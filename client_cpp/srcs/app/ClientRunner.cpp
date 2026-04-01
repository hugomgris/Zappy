#include "app/ClientRunner.hpp"

#include "app/CommandSender.hpp"
#include "../helpers/Logger.hpp"
#include "net/WebsocketClient.hpp"

#include <openssl/opensslv.h>
#include <chrono>
#include <memory>
#include <regex>
#include <thread>

ClientRunner::ClientRunner(const Arguments& args)
	: _args(args), _ws(std::make_unique<WebsocketClient>()), _commands(std::make_unique<CommandSender>(*_ws)), _sawDieEvent(false),
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

Result ClientRunner::handleLoopTextFrame(const std::string& text, int64_t now) {
	Logger::info("Loop frame: " + text);

	if (text.find("\"cmd\"") != std::string::npos && text.find("inventaire") != std::string::npos) {
		const int nourritureCount = tryParseNourritureCount(text);
		if (nourritureCount >= 0) {
			Logger::info("Inventory check: nourriture=" + std::to_string(nourritureCount));
			const int lowFoodThreshold = 6;
			if (nourritureCount <= lowFoodThreshold && now >= _nextFoodPickupEarliestAt) {
				const Result sendTakeFoodRes = _commands->sendPrendNourriture();
				if (!sendTakeFoodRes.ok()) {
					return sendTakeFoodRes;
				}
				Logger::warn("Low food detected; queued prend nourriture");
				_nextFoodPickupEarliestAt = now + 3000;
			}
		}
	}

	const bool hasCmdField = text.find("\"cmd\"") != std::string::npos;
	const bool hasArgField = text.find("\"arg\"") != std::string::npos;
	const bool hasDieValue = text.find("die") != std::string::npos;
	if (hasCmdField && hasArgField && hasDieValue) {
		Logger::warn("Server reported player death; ending loop mode gracefully");
		_sawDieEvent = true;
		return Result::success();
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
			const Result sendVoirRes = _commands->sendVoir();
			if (!sendVoirRes.ok()) {
				return sendVoirRes;
			}
			Logger::info("Periodic voir command queued");
			_nextVoirAt = now + intervalMs;
		}

		if (now >= _nextInventoryAt) {
			const Result sendInvRes = _commands->sendInventaire();
			if (!sendInvRes.ok()) {
				return sendInvRes;
			}
			Logger::info("Periodic inventaire command queued");
			_nextInventoryAt = now + 7000;
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

	const Result loginSendRes = _commands->sendLogin(_args);
	if (!loginSendRes.ok()) {
		return loginSendRes;
	}

	std::string loginReply;
	recvRes = tickUntilTextFrame(4000, loginReply);
	if (!recvRes.ok()) {
		return Result::failure(recvRes.code, "Did not receive login reply: " + recvRes.message);
	}
	Logger::info("Login reply frame: " + loginReply);

	if (containsJsonType(loginReply, "error")) {
		return Result::failure(ErrorCode::ProtocolError, "Server returned error after login: " + loginReply);
	}

	const Result sendVoirRes = _commands->sendVoir();
	if (!sendVoirRes.ok()) {
		return sendVoirRes;
	}

	std::string voirReply;
	recvRes = tickUntilTextFrame(15000, voirReply);
	if (!recvRes.ok()) {
		return Result::failure(
			recvRes.code,
			"Did not receive voir reply: " + recvRes.message
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
