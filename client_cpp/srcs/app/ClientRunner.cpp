#include "app/ClientRunner.hpp"

#include "app/CommandManager.hpp"
#include "app/CommandSender.hpp"
#include "app/command/CommandType.hpp"
#include "app/command/ResourceType.hpp"
#include "app/intent/Intent.hpp"
#include "app/policy/DecisionPolicy.hpp"
#include "app/policy/PeriodicScanPolicy.hpp"
#include "app/policy/WorldModelPolicy.hpp"
#include "../helpers/Logger.hpp"
#include "net/WebsocketClient.hpp"

#include <openssl/opensslv.h>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
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

	std::string commandTypeName(CommandType type) {
		switch (type) {
			case CommandType::Login: return "Login";
			case CommandType::Avance: return "Avance";
			case CommandType::Droite: return "Droite";
			case CommandType::Gauche: return "Gauche";
			case CommandType::Voir: return "Voir";
			case CommandType::Inventaire: return "Inventaire";
			case CommandType::Prend: return "Prend";
			case CommandType::Pose: return "Pose";
			case CommandType::Expulse: return "Expulse";
			case CommandType::Broadcast: return "Broadcast";
			case CommandType::Incantation: return "Incantation";
			case CommandType::Fork: return "Fork";
			case CommandType::ConnectNbr: return "ConnectNbr";
			default: return "Unknown";
		}
	}

	std::optional<std::string> extractJsonStringField(const std::string& text, const std::string& fieldName) {
		const std::string keyToken = "\"" + fieldName + "\"";
		const std::size_t keyPos = text.find(keyToken);
		if (keyPos == std::string::npos) {
			return std::nullopt;
		}

		const std::size_t colonPos = text.find(':', keyPos + keyToken.size());
		if (colonPos == std::string::npos) {
			return std::nullopt;
		}

		std::size_t valuePos = colonPos + 1;
		while (valuePos < text.size() && std::isspace(static_cast<unsigned char>(text[valuePos]))) {
			++valuePos;
		}

		if (valuePos >= text.size() || text[valuePos] != '"') {
			return std::nullopt;
		}

		++valuePos;
		std::size_t closePos = valuePos;
		while (closePos < text.size()) {
			if (text[closePos] == '"' && (closePos == valuePos || text[closePos - 1] != '\\')) {
				return text.substr(valuePos, closePos - valuePos);
			}
			++closePos;
		}

		return std::nullopt;
	}

	std::optional<std::string> extractIncomingBroadcastArg(const std::string& text) {
		const std::optional<std::string> type = extractJsonStringField(text, "type");
		if (!type.has_value() || *type != "message") {
			return std::nullopt;
		}

		return extractJsonStringField(text, "arg");
	}

	bool isServerEventFrame(const std::string& text) {
		const std::optional<std::string> type = extractJsonStringField(text, "type");
		return type.has_value() && *type == "event";
	}

	bool isElevationInProgressFrame(const std::string& text) {
		const std::optional<std::string> event = extractJsonStringField(text, "event");
		const std::optional<std::string> status = extractJsonStringField(text, "status");
		return isServerEventFrame(text)
			&& event.has_value() && *event == "elevation"
			&& status.has_value() && *status == "in_progress";
	}

	bool isElevationCompletionFrame(const std::string& text) {
		if (!isServerEventFrame(text)) {
			return false;
		}

		const std::optional<std::string> status = extractJsonStringField(text, "status");
		if (!status.has_value()) {
			return false;
		}

		if (*status == "Level up!" || *status == "level up!") {
			return true;
		}

		const std::optional<std::string> event = extractJsonStringField(text, "event");
		if (!event.has_value() || *event != "elevation") {
			return false;
		}

		return status.has_value() && (*status == "Level up!" || *status == "level up!" || *status == "ok");
	}

	bool shouldExitOnLocalVictory() {
		const char* value = std::getenv("ZAPPY_EXIT_ON_LOCAL_VICTORY");
		return value != nullptr && std::string(value) == "1";
	}
}

ClientRunner::ClientRunner(const Arguments& args)
	: _args(args), _ws(std::make_unique<WebsocketClient>()), _commands(std::make_unique<CommandSender>(*_ws)),
	  _manager(std::make_unique<CommandManager>([this](const CommandRequest& req) { return dispatchManagedCommand(req); })),
	  _sawDieEvent(false), _lastCmdLayerTraceAtMs(-1) {}

ClientRunner::ClientRunner(const Arguments& args, std::function<Result(const CommandRequest&)> testDispatch)
	: _args(args), _ws(std::make_unique<WebsocketClient>()), _commands(std::make_unique<CommandSender>(*_ws)),
	  _manager(std::make_unique<CommandManager>(std::move(testDispatch))),
	  _sawDieEvent(false), _lastCmdLayerTraceAtMs(-1) {}

ClientRunner::~ClientRunner() = default;

void ClientRunner::setDecisionPolicy(std::unique_ptr<DecisionPolicy> policy) {
	_policy = std::move(policy);
}

void ClientRunner::setIntentCompletionHandler(IntentCompletionHandler handler) {
	_intentCompletionHandler = std::move(handler);
}

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

		case CommandType::Avance:
			return _commands->sendAvance();

		case CommandType::Droite:
			return _commands->sendDroite();

		case CommandType::Gauche:
			return _commands->sendGauche();

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

		case CommandType::Pose: {
			ResourceType resource;
			if (!parseResourceType(req.arg, resource)) {
				return Result::failure(ErrorCode::InvalidArgs, "Unsupported pose resource: '" + req.arg + "'");
			}
			return _commands->sendPose(resource);
		}

		case CommandType::Expulse:
			return _commands->sendExpulse();

		case CommandType::Broadcast:
			return _commands->sendBroadcast(req.arg);

		case CommandType::Incantation:
			return _commands->sendIncantation();

		case CommandType::Fork:
			return _commands->sendFork();

		case CommandType::ConnectNbr:
			return _commands->sendConnectNbr();

		default:
			return Result::failure(ErrorCode::InvalidArgs, "Command type not implemented in dispatch adapter");
	}
}

std::uint64_t ClientRunner::submitIntentAt(const IntentRequest& intent, std::int64_t nowMs) {
	if (!_manager) {
		Logger::warn("submitIntentAt called without initialized command manager");
		return 0;
	}

	auto enqueueTracked = [this, &intent, nowMs](CommandType type, const std::string& arg = "") {
		const std::uint64_t id = _manager->enqueue(type, nowMs, arg);
		if (id != 0) {
			_intentTypeByCommandId[id] = intent.description();
		}
		return id;
	};

	if (dynamic_cast<const RequestVoir*>(&intent) != nullptr) {
		return enqueueTracked(CommandType::Voir);
	}
	if (dynamic_cast<const RequestInventaire*>(&intent) != nullptr) {
		return enqueueTracked(CommandType::Inventaire);
	}
	if (const RequestTake* take = dynamic_cast<const RequestTake*>(&intent)) {
		return enqueueTracked(CommandType::Prend, toProtocolString(take->resource));
	}
	if (const RequestPlace* place = dynamic_cast<const RequestPlace*>(&intent)) {
		return enqueueTracked(CommandType::Pose, toProtocolString(place->resource));
	}
	if (dynamic_cast<const RequestMove*>(&intent) != nullptr) {
		return enqueueTracked(CommandType::Avance);
	}
	if (dynamic_cast<const RequestConnectNbr*>(&intent) != nullptr) {
		return enqueueTracked(CommandType::ConnectNbr);
	}
	if (dynamic_cast<const RequestTurnRight*>(&intent) != nullptr) {
		return enqueueTracked(CommandType::Droite);
	}
	if (dynamic_cast<const RequestTurnLeft*>(&intent) != nullptr) {
		return enqueueTracked(CommandType::Gauche);
	}
	if (const RequestBroadcast* broadcast = dynamic_cast<const RequestBroadcast*>(&intent)) {
		return enqueueTracked(CommandType::Broadcast, broadcast->message);
	}
	if (dynamic_cast<const RequestIncantation*>(&intent) != nullptr) {
		return enqueueTracked(CommandType::Incantation);
	}
	if (dynamic_cast<const RequestFork*>(&intent) != nullptr) {
		return enqueueTracked(CommandType::Fork);
	}

	Logger::warn("submitIntentAt received unsupported intent type");
	return 0;
}

std::uint64_t ClientRunner::submitIntent(const std::shared_ptr<IntentRequest>& intent) {
	if (!intent) {
		Logger::warn("submitIntent called with null intent");
		return 0;
	}

	const std::uint64_t id = submitIntentAt(*intent, nowMs());
	if (id == 0) {
		Logger::warn("Intent submission declined: " + intent->description());
	} else {
		Logger::info("Intent submitted as command id=" + std::to_string(id) + " intent=" + intent->description());
	}
	return id;
}

bool ClientRunner::popCompletedIntent(IntentResult& out) {
	if (_completedIntents.empty()) {
		return false;
	}
	out = _completedIntents.front();
	_completedIntents.pop_front();
	return true;
}

Result ClientRunner::tickCommandLayer(int64_t nowMs) {
	if (!_manager) {
		return Result::success();
	}

	const Result managerTickRes = _manager->tick(nowMs);
	if (!managerTickRes.ok()) {
		return managerTickRes;
	}

	const std::size_t backlog = _manager->queuedCount() + (_manager->hasInFlight() ? 1u : 0u);
	if (_policy && backlog < 8u) {
		const std::size_t budget = 8u - backlog;
		std::size_t submitted = 0;
		std::vector<std::shared_ptr<IntentRequest>> intents = _policy->onTick(nowMs);
		for (const std::shared_ptr<IntentRequest>& intent : intents) {
			if (!intent || submitted >= budget) {
				continue;
			}
			const std::uint64_t id = submitIntentAt(*intent, nowMs);
			if (id == 0) {
				Logger::warn("Policy intent submission declined: " + intent->description());
				continue;
			}
			submitted += 1;
		}
	}

	return handleCompletedCommands(nowMs);
}

Result ClientRunner::processManagedTextFrame(const std::string& text, int64_t nowMs) {
	if (!_manager) {
		return Result::success();
	}

	Logger::trace("RX", "loop frame=" + text);

	if (isServerEventFrame(text)) {
		if (isElevationInProgressFrame(text)) {
			Logger::info("ClientRunner: elevation in progress event: " + text);
			return handleCompletedCommands(nowMs);
		}

		if (isElevationCompletionFrame(text)) {
			Logger::info("ClientRunner: elevation completion event: " + text);
			if (_policy) {
				CommandEvent event;
				event.commandId = 0;
				event.commandType = CommandType::Incantation;
				event.status = CommandStatus::Success;
				event.details = text;

				std::vector<std::shared_ptr<IntentRequest>> followUps = _policy->onCommandEvent(nowMs, event, std::nullopt);
				const std::size_t backlog = _manager->queuedCount() + (_manager->hasInFlight() ? 1u : 0u);
				const std::size_t budget = (backlog >= 8u) ? 0u : (8u - backlog);
				std::size_t submitted = 0;
				for (const std::shared_ptr<IntentRequest>& followUp : followUps) {
					if (!followUp) {
						continue;
					}
					if (submitted >= budget) {
						break;
					}
					const std::uint64_t id = submitIntentAt(*followUp, nowMs);
					if (id == 0) {
						Logger::warn("Policy follow-up intent declined: " + followUp->description());
						continue;
					}
					submitted += 1;
				}
			}

			return handleCompletedCommands(nowMs);
		}

		Logger::info("ClientRunner: ignoring server event frame: " + text);
		return handleCompletedCommands(nowMs);
	}

	const bool consumedByManager = _manager->onServerTextFrame(text);

	if (!consumedByManager && _policy) {
		const std::optional<std::string> incomingBroadcast = extractIncomingBroadcastArg(text);
		if (incomingBroadcast.has_value()) {
			CommandEvent event;
			event.commandId = 0;
			event.commandType = CommandType::Broadcast;
			event.status = CommandStatus::Success;
			event.details = text;

			std::vector<std::shared_ptr<IntentRequest>> followUps = _policy->onCommandEvent(nowMs, event, std::nullopt);
			const std::size_t backlog = _manager->queuedCount() + (_manager->hasInFlight() ? 1u : 0u);
			const std::size_t budget = (backlog >= 8u) ? 0u : (8u - backlog);
			std::size_t submitted = 0;
			for (const std::shared_ptr<IntentRequest>& followUp : followUps) {
				if (!followUp) {
					continue;
				}
				if (submitted >= budget) {
					break;
				}
				const std::uint64_t id = submitIntentAt(*followUp, nowMs);
				if (id == 0) {
					Logger::warn("Policy follow-up intent declined: " + followUp->description());
					continue;
				}
				submitted += 1;
			}
		}
	}

	return handleCompletedCommands(nowMs);
}

Result ClientRunner::tickCommandLayerForTesting(int64_t nowMs) {
	return tickCommandLayer(nowMs);
}

Result ClientRunner::processManagedTextFrameForTesting(const std::string& text, int64_t nowMs) {
	return processManagedTextFrame(text, nowMs);
}

Result ClientRunner::handleCompletedCommands(int64_t nowMs) {
	if (!_manager) {
		return Result::success();
	}

	if (Logger::isDeepTraceEnabled() && (_lastCmdLayerTraceAtMs < 0 || (nowMs - _lastCmdLayerTraceAtMs) >= 500)) {
		Logger::trace("CMD_LAYER", "tick now=" + std::to_string(nowMs)
			+ " queued=" + std::to_string(_manager->queuedCount())
			+ " inFlight=" + std::string(_manager->hasInFlight() ? "1" : "0"));
		_lastCmdLayerTraceAtMs = nowMs;
	}

	CommandResult completed;
	while (_manager->popCompleted(completed)) {
		const auto intentIt = _intentTypeByCommandId.find(completed.id);
		const bool hasTrackedIntent = (intentIt != _intentTypeByCommandId.end());
		const std::string intentType = hasTrackedIntent
			? intentIt->second
			: ("Command(" + commandTypeName(completed.type) + ")");

		IntentResult intentResult(
			completed.id,
			intentType,
			completed.status == CommandStatus::Success,
			completed.details
		);
		_completedIntents.push_back(intentResult);
		if (_intentCompletionHandler) {
			_intentCompletionHandler(intentResult);
		}
		if (hasTrackedIntent) {
			_intentTypeByCommandId.erase(intentIt);
		}

		CommandEvent event;
		event.commandId = completed.id;
		event.commandType = completed.type;
		event.status = completed.status;
		event.details = completed.details;
		if (_policy) {
			std::optional<IntentResult> optIntent = hasTrackedIntent ? std::optional<IntentResult>(intentResult) : std::nullopt;
			std::vector<std::shared_ptr<IntentRequest>> followUps = _policy->onCommandEvent(nowMs, event, optIntent);
			const std::size_t backlog = _manager->queuedCount() + (_manager->hasInFlight() ? 1u : 0u);
			const std::size_t budget = (backlog >= 8u) ? 0u : (8u - backlog);
			std::size_t submitted = 0;
			for (const std::shared_ptr<IntentRequest>& followUp : followUps) {
				if (!followUp) {
					continue;
				}
				if (submitted >= budget) {
					break;
				}
				const std::uint64_t id = submitIntentAt(*followUp, nowMs);
				if (id == 0) {
					Logger::warn("Policy follow-up intent declined: " + followUp->description());
					continue;
				}
				Logger::trace("INTENT", "follow-up submitted id=" + std::to_string(id)
					+ " desc=" + followUp->description());
				submitted += 1;
			}
		}

		if (completed.status != CommandStatus::Success) {
			if (completed.status == CommandStatus::ServerError) {
				Logger::warn("Command rejected by server (continuing): " + commandTypeName(completed.type)
					+ " -> " + completed.details);
				continue;
			}

			if (_args.loopMode) {
				Logger::warn("Loop mode: transient command failure (continuing): "
					+ commandTypeName(completed.type) + " -> " + completed.details);
				continue;
			}

			const ErrorCode mappedError =
				(completed.status == CommandStatus::NetworkError || completed.status == CommandStatus::Timeout)
					? ErrorCode::NetworkError
					: ErrorCode::ProtocolError;
			return Result::failure(mappedError, "Command failed: " + completed.details);
		}

		Logger::info("Completed command id=" + std::to_string(completed.id) +
			" type=" + std::to_string(static_cast<int>(completed.type)));
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

	return processManagedTextFrame(text, now);
}

Result ClientRunner::runPersistentLoop(int intervalMs) {
	_sawDieEvent = false;
	if (!_policy) {
		_policy = std::make_unique<WorldModelPolicy>(intervalMs, 2000);
	}

	while (true) {
		const int64_t now = nowMs();
		const Result tickRes = _ws->tick(now);
		if (!tickRes.ok()) {
			if (_sawDieEvent) {
				return Result::success();
			}
			return tickRes;
		}

		const Result commandTickRes = tickCommandLayer(now);
		if (!commandTickRes.ok()) {
			return commandTickRes;
		}

		const WorldModelPolicy* worldModel = dynamic_cast<const WorldModelPolicy*>(_policy.get());
		if (shouldExitOnLocalVictory() && worldModel != nullptr && worldModel->victoryReached()) {
			Logger::info("ClientRunner: policy reached victory threshold, stopping loop mode");
			return Result::success();
		}

		while (true) {
			WebSocketFrame frame;
			const IoResult recvRes = _ws->recvFrame(frame);
			if (recvRes.status == NetStatus::WouldBlock) {
				break;
			}
			if (recvRes.status != NetStatus::Ok) {
				return Result::failure(ErrorCode::NetworkError, "Loop receive failed: " + recvRes.message);
			}

			if (frame.opcode != WebSocketOpcode::Text) {
				continue;
			}

			std::string text(frame.payload.begin(), frame.payload.end());
			const Result frameRes = handleLoopTextFrame(text, now);
			if (!frameRes.ok()) {
				return frameRes;
			}
			if (_sawDieEvent) {
				return Result::success();
			}

			if (shouldExitOnLocalVictory() && worldModel != nullptr && worldModel->victoryReached()) {
				Logger::info("ClientRunner: policy reached victory threshold, stopping loop mode");
				return Result::success();
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
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
