#include "SimpleAI.hpp"
#include "helpers/Logger.hpp"

#include <chrono>

namespace zappy {
	SimpleAI::SimpleAI(WorldState& state, CommandSender& sender)
    : _state(state), _sender(sender) {}

	void SimpleAI::tick(int64_t nowMs) {
		// timeout processing
		_sender.checkTimeouts();

		// preiodic sensor updates
		if (nowMs - _lastVoirTime > 3000) {
			requestVision();
		}
		if (nowMs - _lastInventaireTime > 5000) {
			requestInventory();
		}

		// if ai is awaiting for a response, don't send new cmds
		if (_sender.pendingCount() > 0) return;

		// avoid spamming
		if (nowMs - _lastCommandTime < 200) return;

		// execute queued actions first
		if (!_actionQueue.empty()) {
			executeNextAction();
			return;
		}

		decideNextAction(nowMs);
	}

	void SimpleAI::decideNextAction(int64_t nowMs) {
		// priority design
		// 1 - survival, get food if low
		// 2 - level up, throw incantation if ready
		// 3 - gather, get stones needed for next lvl
		// 4 - expand, fork if conditions are met
		// 5 - explore, wander looking for rsrcs

		if (shouldGetFood()) {
			Logger::info("AI: Low food (" + std::to_string(_state.getFood()) + "), seeking nourriture");
			startGathering("nourriture");
			return;
		}
		
		if (shouldIncantate(nowMs)) {
			Logger::info("AI: Ready to incantate for level " + std::to_string(_state.getLevel() + 1));
			startIncantation();
			return;
		}
		
		auto missingStones = _state.getMissingStones();
		if (!missingStones.empty()) {
			Logger::info("AI: Need " + std::to_string(missingStones.size()) + " stones for next level");
			startGathering(missingStones[0]);
			return;
		}
		
		if (shouldFork(nowMs)) {
			Logger::info("AI: Forking to increase team size");
			startFork();
			return;
		}

		// exploration
		Logger::debug("AI:: Exploring");
		_AIstate = AIState::Exploring;
		auto plan = _planner.planExploration(_state);
		for (const auto& step : plan) {
			_actionQueue.push(step);
		}

		executeNextAction();
	}

	bool SimpleAI::shouldGetFood() const {
		return _state.getFood() < _foodEmergencyThreshold;
	}

	bool SimpleAI::shouldFork(int64_t nowMs) const {
		if (!_forkEnabled) return false;
		if (_forkCount >= _maxForks) return false;
		if (_state.getFood() < _forkFoodThreshold) return false;
		if (nowMs - _lastForkTime < 10000) return false;  // 10 second cooldown
		return true;
	}

	bool SimpleAI::shouldIncantate(int64_t nowMs) const {
		if (_state.getLevel() >= _targetLevel) return false;
		if (nowMs - _lastIncantationTime < 5000) return false;  // 5 second cooldown
		return _state.canIncantate();
	}

	std::vector<std::string> SimpleAI::getResourcePriorities() const {
		// food ALWAYS FIRST if below comfort level
		std::vector<std::string> priorities;

		if (_state.getFood() < _foodComfortThreshold) {
			priorities.push_back("nourriture");
		}

		// then, stones following next level needs
		auto missing = _state.getMissingStones();
		priorities.insert(priorities.end(), missing.begin(), missing.end());

		// general priority order for remaining stones
		static const std::vector<std::string> stoneOrder = {
			"linemate", "deraumere", "sibur", "mendiane", "phiras", "thystame"
		};
		for (const auto& stone : stoneOrder) {
			if (std::find(priorities.begin(), priorities.end(), stone) == priorities.end()) {
				priorities.push_back(stone);
			}
		}

		return priorities;
	}

	void SimpleAI::startGathering(const std::string& resource) {
		_AIstate = AIState::Gathering;
		_currentResourceTarget = resource;

		auto plan = _planner.planPathToResource(_state, resource);

		if (plan.empty()) {
			Logger::warn("AI: No path to " + resource + ", exploring instead");
			_AIstate = AIState::Exploring;
			plan = _planner.planExploration(_state);
		}

		for (const auto& step : plan) {
			_actionQueue.push(step);
		}

		executeNextAction();
	}

	void SimpleAI::startIncantation() {
		_AIstate = AIState::Incantating;
		_lastIncantationTime = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();

		_sender.sendIncantation();
		_pendingCommandId = _sender.expectResponse("incantation", [this](const ServerMessage& msg) { onCommandComplete(msg); });
		_lastCommandTime = _lastIncantationTime;
	}

	void SimpleAI::startFork() {
		_AIstate = AIState::WaitingForResponse;
		_lastForkTime = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();
		
		_sender.sendFork();
		_pendingCommandId = _sender.expectResponse("fork",
			[this](const ServerMessage& msg) { onCommandComplete(msg); });
		_lastCommandTime = _lastForkTime;
		
		_forkCount++;
	}

	void SimpleAI::requestVision() {
		_lastVoirTime = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();
		_sender.sendVoir();
	}

	void SimpleAI::requestInventory() {
		_lastInventaireTime = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();
		_sender.sendInventaire();
	}

	void SimpleAI::executeNextAction() {
		if (_actionQueue.empty()) {
			_AIstate = AIState::Idle;
			return;
		}

		auto step = _actionQueue.front();
		_actionQueue.pop();

		int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();
		_lastCommandTime = now;
		
		std::string cmdName;

		switch (step.action) {
			case NavAction::MoveForward:
				_sender.sendAvance();
				cmdName = "avance";
				break;

			case NavAction::TurnLeft:
				_sender.sendGauche();
				cmdName = "gauche";
				break;

			case NavAction::TurnRight:
				_sender.sendDroite();
				cmdName = "droite";
				break;

			case NavAction::Take:
				_sender.sendPrend(step.resource);
				cmdName = "prend";
				break;

			case NavAction::Place:
				_sender.sendPose(step.resource);
				cmdName = "pose";
				break;
				
			default:
				return;
		}

		_pendingCommandId = _sender.expectResponse(cmdName,
			[this](const ServerMessage& msg) { onCommandComplete(msg); });

		Logger::debug("AI: Executing " + step.toString());
	}

	void SimpleAI::onCommandComplete(const ServerMessage& msg) {
		if (!msg.isOk()) {
			Logger::warn("AI: Command failed: " + msg.cmd);

			// queue needs to be cleard on failure
			while (!_actionQueue.empty()) _actionQueue.pop();
			_AIstate = AIState::Idle;
			return;
		}

		// state update based con cmd
		_state.onResponse(msg);

		if (!_actionQueue.empty()) {
			executeNextAction();
		} else {
			_AIstate = AIState::Idle;
		}
	}
} // namspace zappy