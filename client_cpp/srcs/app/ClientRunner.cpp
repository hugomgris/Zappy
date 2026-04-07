#include "app/ClientRunner.hpp"

#include "app/CommandManager.hpp"
#include "app/CommandSender.hpp"
#include "app/command/CommandType.hpp"
#include "app/command/ResourceType.hpp"
#include "../helpers/Logger.hpp"
#include "net/WebsocketClient.hpp"

#include <openssl/opensslv.h>
#include <chrono>
#include <memory>
#include <regex>
#include <thread>

namespace {
	bool parseResourceType(const std::string& value, ResourceType& out) {
		if (value == "nourriture") {
			out = ResourceType::Nourriture;
			return true;
		}
		if (value == "linemate") {
			out = ResourceType::Linemate;
			return true;
		}
		if (value == "deraumere") {
			out = ResourceType::Deraumere;
			return true;
		}
		if (value == "sibur") {
			out = ResourceType::Sibur;
			return true;
		}
		if (value == "mendiane") {
			out = ResourceType::Mendiane;
			return true;
		}
		if (value == "phiras") {
			out = ResourceType::Phiras;
			return true;
		}
		if (value == "thystame") {
			out = ResourceType::Thystame;
			return true;
		}
		return false;
	}
}

ClientRunner::ClientRunner(const Arguments& args)
	: _args(args), _ws(std::make_unique<WebsocketClient>()), _commands(std::make_unique<CommandSender>(*_ws)),
	  _manager(std::make_unique<CommandManager>([this](const CommandRequest& req) { return dispatchManagedCommand(req); })), _sawDieEvent(false),
	  _nextVoirAt(0), _nextInventoryAt(0), _nextFoodPickupEarliestAt(0) {}

ClientRunner::~ClientRunner() = default;

int64_t ClientRunner::nowMs() {
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

Result ClientRunner::tickUntilConnected(int timeoutMs) {
	const int64_t deadline = nowMs() + timeoutMs;
	while (nowMs() < deadline) {
		const Result tickRes = _ws->tick(nowMs());
		if (!tickRes.ok()) {
			return tickRes;
		}
		if (_ws->isConnected()) {
			return Result::success();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return Result::failure(ErrorCode::NetworkError, "Timed out waiting for WebSocket connection");
}

Result ClientRunner::tickUntilTextFrame(int timeoutMs, std::string& outText) {
	const int64_t deadline = nowMs() + timeoutMs;
	while (nowMs() < deadline) {
		const Result tickRes = _ws->tick(nowMs());
		if (!tickRes.ok()) {
			return tickRes;
		}

		WebSocketFrame frame;
		const IoResult recvRes = _ws->recvFrame(frame);
		if (recvRes.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
			outText.assign(frame.payload.begin(), frame.payload.end());
			return Result::success();
		}
		if (recvRes.status != NetStatus::WouldBlock && recvRes.status != NetStatus::Ok) {
			return Result::failure(ErrorCode::NetworkError, "Frame receive failed: " + recvRes.message);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return Result::failure(ErrorCode::NetworkError, "Timed out waiting for text frame");
}

Result ClientRunner::tickForDuration(int durationMs) {
	const int64_t deadline = nowMs() + durationMs;
	while (nowMs() < deadline) {
		const Result tickRes = _ws->tick(nowMs());
		if (!tickRes.ok()) {
			return tickRes;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return Result::success();
}

Result ClientRunner::waitForManagedCommandCompletion(CommandType expectedType, int timeoutMs, std::string& outDetails) {
	if (!_manager) {
		return Result::failure(ErrorCode::InternalError, "Command manager is not initialized");
	}

	const int64_t deadline = nowMs() + timeoutMs;
	while (nowMs() < deadline) {
		const int64_t now = nowMs();

		const Result wsTickRes = _ws->tick(now);
		if (!wsTickRes.ok()) {
			return wsTickRes;
		}

		const Result managerTickRes = _manager->tick(now);
		if (!managerTickRes.ok()) {
			return managerTickRes;
		}

		WebSocketFrame frame;
		const IoResult recvRes = _ws->recvFrame(frame);
		if (recvRes.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
			std::string text(frame.payload.begin(), frame.payload.end());
			_manager->onServerTextFrame(text);
		}
		if (recvRes.status != NetStatus::WouldBlock && recvRes.status != NetStatus::Ok) {
			return Result::failure(ErrorCode::NetworkError, "Frame receive failed: " + recvRes.message);
		}

		CommandResult completed;
		while (_manager->popCompleted(completed)) {
			if (completed.type != expectedType) {
				continue;
			}

			outDetails = completed.details;
			if (completed.status == CommandStatus::Success) {
				return Result::success();
			}

			const ErrorCode mappedError =
				(completed.status == CommandStatus::NetworkError || completed.status == CommandStatus::Timeout)
					? ErrorCode::NetworkError
					: ErrorCode::ProtocolError;
			return Result::failure(mappedError, completed.details);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return Result::failure(ErrorCode::NetworkError, "Timed out waiting for managed command completion");
}

Result ClientRunner::dispatchManagedCommand(const CommandRequest& req) {
	switch (req.type) {
		case CommandType::Login:
			return _commands->sendLogin(_args);

		case CommandType::Voir:
			return _commands->sendVoir();

		case CommandType::Inventaire:
			return _commands->sendInventaire();

		case CommandType::Prend: {
			ResourceType resource;
			if (!parseResourceType(req.arg, resource)) {
				return Result::failure(ErrorCode::InvalidArgs, "Unsupported prend resource: '" + req.arg + "'");
			}
			return _commands->sendPrend(resource);
		}

		default:
			return Result::failure(ErrorCode::InvalidArgs, "Command type not implemented in dispatch adapter");
	}
}

Result ClientRunner::handleCompletedCommands(int64_t nowMs) {
	if (!_manager) {
		return Result::success();
	}

	CommandResult completed;
	while (_manager->popCompleted(completed)) {
		if (completed.status != CommandStatus::Success) {
			if (completed.status == CommandStatus::ServerError && completed.type == CommandType::Prend) {
				Logger::warn("Prend failed: " + completed.details);
				continue;
			}

			const ErrorCode mappedError =
				(completed.status == CommandStatus::NetworkError || completed.status == CommandStatus::Timeout)
					? ErrorCode::NetworkError
					: ErrorCode::ProtocolError;
			return Result::failure(mappedError, "Command failed: " + completed.details);
		}

		if (completed.type == CommandType::Inventaire) {
			const int nourritureCount = tryParseNourritureCount(completed.details);
			if (nourritureCount >= 0) {
				Logger::info("Inventory check: nourriture=" + std::to_string(nourritureCount));
				const int lowFoodThreshold = 6;
				if (nourritureCount <= lowFoodThreshold && nowMs >= _nextFoodPickupEarliestAt) {
					_manager->enqueue(CommandType::Prend, nowMs, "nourriture");
					Logger::warn("Low food detected; queued prend nourriture");
					_nextFoodPickupEarliestAt = nowMs + 3000;
				}
			}
		}
	}

	return Result::success();
}

bool ClientRunner::isDieEventFrame(const std::string& text) const {
	const bool hasCmdField = text.find("\"cmd\"") != std::string::npos;
	const bool hasArgField = text.find("\"arg\"") != std::string::npos;
	const bool hasDieValue = text.find("die") != std::string::npos;
	return hasCmdField && hasArgField && hasDieValue;
}

Result ClientRunner::handleLoopTextFrame(const std::string& text, int64_t now) {
	Logger::info("Loop frame: " + text);

	if (isDieEventFrame(text)) {
		Logger::warn("Server reported player death; ending loop mode gracefully");
		_sawDieEvent = true;
		return Result::success();
	}

	if (_manager) {
		_manager->onServerTextFrame(text);
		return handleCompletedCommands(now);
	}

	return Result::success();
}

Result ClientRunner::runPersistentLoop(int intervalMs) {
	_nextVoirAt = nowMs() + intervalMs;
	_nextInventoryAt = nowMs() + 2000;
	_nextFoodPickupEarliestAt = nowMs();
	_sawDieEvent = false;

	while (true) {
		const int64_t now = nowMs();
		const Result tickRes = _ws->tick(now);
		if (!tickRes.ok()) {
			if (_sawDieEvent) {
				return Result::success();
			}
			return tickRes;
		}

		if (now >= _nextVoirAt) {
			if (_manager) {
				_manager->enqueue(CommandType::Voir, now);
			}
			Logger::info("Periodic voir command queued");
			_nextVoirAt = now + intervalMs;
		}

		if (now >= _nextInventoryAt) {
			if (_manager) {
				_manager->enqueue(CommandType::Inventaire, now);
			}
			Logger::info("Periodic inventaire command queued");
			_nextInventoryAt = now + 7000;
		}

		if (_manager) {
			const Result managerTickRes = _manager->tick(now);
			if (!managerTickRes.ok()) {
				return managerTickRes;
			}
			const Result completedRes = handleCompletedCommands(now);
			if (!completedRes.ok()) {
				return completedRes;
			}
		}

		WebSocketFrame frame;
		const IoResult recvRes = _ws->recvFrame(frame);
		if (recvRes.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
			std::string text(frame.payload.begin(), frame.payload.end());
			const Result frameRes = handleLoopTextFrame(text, now);
			if (!frameRes.ok()) {
				return frameRes;
			}
			if (_sawDieEvent) {
				return Result::success();
			}
		}
		if (recvRes.status != NetStatus::WouldBlock && recvRes.status != NetStatus::Ok) {
			return Result::failure(ErrorCode::NetworkError, "Loop receive failed: " + recvRes.message);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

bool ClientRunner::containsJsonType(const std::string& payload, const std::string& typeValue) const {
	return payload.find("\"type\":\"" + typeValue + "\"") != std::string::npos
		|| payload.find("\"type\": \"" + typeValue + "\"") != std::string::npos;
}

int ClientRunner::tryParseNourritureCount(const std::string& text) const {
	std::smatch match;
	const std::regex foodRegex("\\\"nourriture\\\"\\s*:\\s*(\\d+)");
	if (std::regex_search(text, match, foodRegex) && match.size() > 1) {
		return std::stoi(match[1].str());
	}
	return -1;
}

Result ClientRunner::runTransportFlow() {
	Result connectRes = _ws->connect(_args.hostname, _args.port, _args.insecure);
	if (!connectRes.ok()) {
		return Result::failure(connectRes.code, "Connect start failed: " + connectRes.message);
	}

	connectRes = tickUntilConnected(5000);
	if (!connectRes.ok()) {
		return connectRes;
	}

	std::string firstServerMsg;
	Result recvRes = tickUntilTextFrame(2000, firstServerMsg);
	if (recvRes.ok()) {
		Logger::info("Initial server frame: " + firstServerMsg);
	}

	if (_manager) {
		_manager->enqueue(CommandType::Login, nowMs());
	}

	std::string loginReply;
	const Result loginWaitRes = waitForManagedCommandCompletion(CommandType::Login, 6000, loginReply);
	if (!loginWaitRes.ok()) {
		return Result::failure(loginWaitRes.code, "Did not receive login reply: " + loginWaitRes.message);
	}
	Logger::info("Login reply frame: " + loginReply);

	if (_manager) {
		_manager->enqueue(CommandType::Voir, nowMs());
	}

	std::string voirReply;
	const Result voirWaitRes = waitForManagedCommandCompletion(CommandType::Voir, 15000, voirReply);
	if (!voirWaitRes.ok()) {
		return Result::failure(
			voirWaitRes.code,
			"Did not receive voir reply: " + voirWaitRes.message
				+ " (transport/login are OK; server may be paused or not scheduling game responses yet. "
				+ "For continuous behavior, run with --loop)"
		);
	}
	Logger::info("Voir reply frame: " + voirReply);

	if (_args.loopMode) {
		Logger::info("Loop mode enabled: keeping connection alive with periodic voir commands");
		return runPersistentLoop(5000);
	}

	const Result holdRes = tickForDuration(5000);
	if (!holdRes.ok()) {
		return Result::failure(holdRes.code, "Connection dropped during hold window: " + holdRes.message);
	}
	Logger::info("Connection remained stable during post-command hold window");

	_ws->close();
	return Result::success();
}

Result ClientRunner::run() {
	Logger::info("OpenSSL version: " + std::string(OPENSSL_VERSION_TEXT));
	return runTransportFlow();
}
