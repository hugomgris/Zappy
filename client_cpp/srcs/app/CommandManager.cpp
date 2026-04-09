#include "app/CommandManager.hpp"
#include "helpers/Logger.hpp"

#include <utility>

namespace {
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
}

CommandManager::CommandManager(DispatchFn dispatch)
	: _dispatch(std::move(dispatch)), _nextId(1) {}

std::uint64_t CommandManager::enqueue(CommandType type, std::int64_t nowMs, const std::string& arg) {
	// Guard against queue overflow
	if (_pending.size() >= MAX_PENDING_COMMANDS) {
		Logger::warn("CommandManager: Queue overflow prevented. Max pending (" +
			std::to_string(MAX_PENDING_COMMANDS) + ") reached. Dropping enqueue.");
		return 0; // 0 indicates failure
	}

	const std::uint64_t id = _nextId++;
	_pending.push_back(CommandRequest::make(id, type, nowMs, arg));
	Logger::trace("CMD_ENQUEUE", "id=" + std::to_string(id)
		+ " type=" + commandTypeName(type)
		+ " arg='" + arg + "'"
		+ " pending=" + std::to_string(_pending.size()));
	Logger::info("CommandManager: Enqueued command " + std::to_string(id) +
		" (type=" + std::to_string(static_cast<int>(type)) + "), queue size=" + std::to_string(_pending.size()));
	return id;
}

Result CommandManager::tick(std::int64_t nowMs) {
	startNextIfIdle();

	// Check for stale in-flight command
	checkStaleFlight(nowMs);

	if (_inFlight && nowMs >= _inFlight->deadlineAtMs) {
		Logger::trace("CMD_TIMEOUT", "id=" + std::to_string(_inFlight->id)
			+ " type=" + commandTypeName(_inFlight->type)
			+ " retry=" + std::to_string(_inFlight->retryCount)
			+ "/" + std::to_string(_inFlight->spec.maxRetries));
		if (_inFlight->retryCount < _inFlight->spec.maxRetries) {
			_inFlight->retryCount += 1;
			_inFlight->deadlineAtMs = nowMs + _inFlight->spec.timeoutMs;

			Logger::info("CommandManager: Retrying command " + std::to_string(_inFlight->id) +
				" (attempt " + std::to_string(_inFlight->retryCount) + "/" +
				std::to_string(_inFlight->spec.maxRetries) + ")");

			const Result dispatchRes = dispatchInFlight();
			if (!dispatchRes.ok()) {
				completeInFlight(CommandStatus::NetworkError,
					"Dispatch failed on retry: " + dispatchRes.message);
			}
		} else {
			Logger::warn("CommandManager: Command " + std::to_string(_inFlight->id) +
				" timeout after " + std::to_string(_inFlight->retryCount) + " retries");
			completeInFlight(CommandStatus::Timeout,
				"Exhausted retries (" + std::to_string(_inFlight->spec.maxRetries) + ")");
		}
	}

	startNextIfIdle();
	return Result::success();
}

bool CommandManager::onServerTextFrame(const std::string& text) {
	if (!_inFlight) {
		return false;
	}

	Logger::trace("RX", "inFlight id=" + std::to_string(_inFlight->id)
		+ " type=" + commandTypeName(_inFlight->type)
		+ " frame=" + text);

	// Use new matcher for comprehensive validation
	MatchResult matchResult = CommandReplyMatcher::validateReply(_inFlight->type, text);

	if (matchResult.status == CommandStatus::ServerError) {
		// Error or ko frame
		Logger::warn("CommandManager: Command " + std::to_string(_inFlight->id) +
			" received server error: " + matchResult.details);
		completeInFlight(CommandStatus::ServerError, matchResult.details);
		startNextIfIdle();
		return true;
	}

	if (matchResult.status == CommandStatus::MalformedReply) {
		// Malformed JSON
		Logger::warn("CommandManager: Command " + std::to_string(_inFlight->id) +
			" received malformed reply: " + matchResult.details);
		completeInFlight(CommandStatus::MalformedReply, matchResult.details);
		startNextIfIdle();
		return true;
	}

	if (matchResult.status == CommandStatus::UnexpectedReply) {
		// Frame for different command, don't complete just yet
		// (it might be for something else or out-of-order)
		Logger::info("CommandManager: Ignoring unexpected reply for command " +
			std::to_string(_inFlight->id) + ": " + matchResult.details);
		Logger::trace("CMD_MATCH", "unexpected for id=" + std::to_string(_inFlight->id)
			+ " details=" + matchResult.details);
		return false;
	}

	if (matchResult.isMatch && matchResult.status == CommandStatus::Success) {
		Logger::info("CommandManager: Command " + std::to_string(_inFlight->id) + " succeeded");
		completeInFlight(CommandStatus::Success, matchResult.details);
		startNextIfIdle();
		return true;
	}

	return false;
}

bool CommandManager::hasInFlight() const {
	return _inFlight.has_value();
}

std::size_t CommandManager::queuedCount() const {
	return _pending.size();
}

bool CommandManager::isFull() const {
	return _pending.size() >= MAX_PENDING_COMMANDS;
}

bool CommandManager::popCompleted(CommandResult& out) {
	if (_completed.empty()) {
		return false;
	}
	out = _completed.front();
	_completed.pop_front();
	return true;
}

Result CommandManager::dispatchInFlight() {
	if (!_inFlight) {
		return Result::failure(ErrorCode::InternalError, "No in-flight command to dispatch");
	}
	if (!_dispatch) {
		return Result::failure(ErrorCode::InternalError, "No dispatcher configured");
	}
	return _dispatch(*_inFlight);
}

void CommandManager::startNextIfIdle() {
	if (_inFlight || _pending.empty()) {
		return;
	}

	_inFlight = _pending.front();
	_pending.pop_front();
	Logger::trace("CMD_DISPATCH", "start id=" + std::to_string(_inFlight->id)
		+ " type=" + commandTypeName(_inFlight->type)
		+ " pendingAfterPop=" + std::to_string(_pending.size())
		+ " deadline=" + std::to_string(_inFlight->deadlineAtMs));

	const Result dispatchRes = dispatchInFlight();
	if (!dispatchRes.ok()) {
		Logger::warn("CommandManager: Initial dispatch failed for command " +
			std::to_string(_inFlight->id) + ": " + dispatchRes.message);
		completeInFlight(CommandStatus::NetworkError,
			"Dispatch failed: " + dispatchRes.message);
	} else {
		Logger::info("CommandManager: Dispatched command " + std::to_string(_inFlight->id));
	}
}

void CommandManager::checkStaleFlight(std::int64_t nowMs) {
	if (!_inFlight) {
		return;
	}

	// Consider in-flight stale if it's still waiting after deadline + STALE_INFLIGHT_MS
	// This handles cases where the deadline keeps getting extended by retries
	std::int64_t staleBoundary = _inFlight->enqueuedAtMs + STALE_INFLIGHT_MS;
	if (nowMs > staleBoundary) {
		Logger::error("CommandManager: Detected stale in-flight command " +
			std::to_string(_inFlight->id) + " (enqueued " +
			std::to_string(nowMs - _inFlight->enqueuedAtMs) + "ms ago). Force-completing as timeout.");
		completeInFlight(CommandStatus::Timeout,
			"Stale command (no response for " +
			std::to_string(STALE_INFLIGHT_MS) + "ms)");
	}
}

void CommandManager::completeInFlight(CommandStatus status, const std::string& details) {
	if (!_inFlight) {
		return;
	}

	CommandResult result;
	result.id = _inFlight->id;
	result.type = _inFlight->type;
	result.status = status;
	result.details = details;
	_completed.push_back(result);
	Logger::trace("CMD_DONE", "id=" + std::to_string(result.id)
		+ " type=" + commandTypeName(result.type)
		+ " status=" + std::to_string(static_cast<int>(result.status))
		+ " details=" + result.details);
	
	Logger::info("CommandManager: Command " + std::to_string(_inFlight->id) +
		" completed with status=" + std::to_string(static_cast<int>(status)));
	
	// Notify observers of completion event
	CommandEvent event;
	event.commandId = result.id;
	event.commandType = result.type;
	event.status = result.status;
	event.details = result.details;
	notifyCompletion(event);
	
	_inFlight.reset();
}

void CommandManager::notifyCompletion(const CommandEvent& event) {
	if (_eventHandler) {
		_eventHandler(event);
	}
}
