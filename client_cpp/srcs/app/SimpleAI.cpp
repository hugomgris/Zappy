#include "SimpleAI.hpp"
#include "helpers/Logger.hpp"

#include <chrono>
#include <cstdlib>
#include <sstream>

namespace zappy {
	SimpleAI::SimpleAI(WorldState& state, CommandSender& sender)
    : _state(state), _sender(sender) {
		const char* easyAsc = std::getenv("ZAPPY_EASY_ASCENSION");
		_easyAscensionMode = (easyAsc != nullptr && std::string(easyAsc) == "1");
	}

	void SimpleAI::tick(int64_t nowMs) {
		// timeout processing
		_sender.checkTimeouts();

		static int64_t lastPendingLog = 0;
		if (nowMs - lastPendingLog > 5000) {
			size_t pending = _sender.pendingCount();
			if (pending > 0) {
				Logger::warn("Still waiting for " + std::to_string(pending) + " responses");
			}
			lastPendingLog = nowMs;
		}

		// DEBUG: Show AI state
		static int64_t lastStateLog = 0;
		if (nowMs - lastStateLog > 2000) {
			Logger::info("AI State: " + std::to_string(static_cast<int>(_AIstate)) + 
						", pending=" + std::to_string(_sender.pendingCount()) +
						", queue=" + std::to_string(_actionQueue.size()));
			lastStateLog = nowMs;
		}

		// periodic sensor updates
		if (nowMs - _lastVoirTime > 3000) {
			requestVision();
		}
		if (nowMs - _lastInventaireTime > 5000) {
			requestInventory();
		}

		// if ai is awaiting for a response, don't send new cmds
		if (_sender.pendingCount() > 0) {
			Logger::debug("Waiting for " + std::to_string(_sender.pendingCount()) + " responses");
			return;
		}

		// avoid spamming
		if (nowMs - _lastCommandTime < 200) return;

		// execute queued actions first
		if (!_actionQueue.empty()) {
			executeNextAction();
			return;
		}

		// DEBUG TO DO TODO
		static int64_t lastDebugTime = 0;
		if (nowMs - lastDebugTime > 5000) {
			const auto& vision = _state.getVision();
			Logger::info("Vision size: " + std::to_string(vision.size()));
			if (!vision.empty()) {
				Logger::info("Tile 0 has " + std::to_string(vision[0].items.size()) + " items");
				for (const auto& item : vision[0].items) {
					Logger::info("  - " + item);
				}
			} else {
				Logger::warn("NO VISION DATA - Check voir command responses");
			}
			lastDebugTime = nowMs;
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

		if (_state.getVision().empty()) {
			Logger::debug("Waiting for vision data before deciding next action");
			return;
		}

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
		Logger::debug("AI: Exploring");
		_AIstate = AIState::Exploring;
		auto plan = _planner.planExploration(_state);
		for (const auto& step : plan) {
			_actionQueue.push(step);
		}

		executeNextAction();
	}

	bool SimpleAI::shouldGetFood() const {
		int food = _state.getFood();
		
		// Emergency: always get food if below threshold
		if (food < _foodEmergencyThreshold) return true;
		
		// ALWAYS pick up food if on current tile (regardless of food level)
		const auto& vision = _state.getVision();
		if (!vision.empty() && vision[0].hasItem("nourriture")) {
			Logger::debug("Food available on current tile, picking up");
			return true;
		}
		
		// Also check if we see food nearby and have low inventory
		if (food < _foodComfortThreshold && _state.seesItem("nourriture")) {
			auto nearest = _state.getNearestItem("nourriture");
			if (nearest.has_value() && nearest->distance <= 2) {
				return true;
			}
		}
		return false;
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
		if (_easyAscensionMode) return true;
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

		// Check if resource is on current tile
		const auto& vision = _state.getVision();
		if (!vision.empty()) {
			const auto& currentTile = vision[0];
			if (currentTile.hasItem(resource)) {
				Logger::info("AI: " + resource + " is on current tile! Taking immediately.");
				_sender.sendPrend(resource);
				_pendingCommandId = _sender.expectResponse("prend", 
					[this, resource](const ServerMessage& msg) { 
						if (msg.isOk()) {
							Logger::info("AI: Successfully took " + resource);
						} else {
							Logger::warn("AI: Failed to take " + resource);
						}
						onCommandComplete(msg); 
					});
				_lastCommandTime = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()
				).count();
				return;
			}
		}

		auto plan = _planner.planPathToResource(_state, resource);

		// DEBUG: Log what planPathToResource returned
		Logger::info("planPathToResource returned " + std::to_string(plan.size()) + " steps");

		if (plan.empty()) {
			Logger::warn("AI: No path to " + resource + ", exploring instead");
			_AIstate = AIState::Exploring;
			plan = _planner.planExploration(_state);
			Logger::info("planExploration returned " + std::to_string(plan.size()) + " steps");
			
			// Make sure we have a plan
			if (plan.empty()) {
				Logger::error("AI: Exploration plan is also empty!");
				_AIstate = AIState::Idle;
				return;
			}
		}

		Logger::info("AI: Starting to gather " + resource + " with " + std::to_string(plan.size()) + " steps");
		for (const auto& step : plan) {
			_actionQueue.push(step);
			Logger::info("  QUEUED Step: " + step.toString());
		}

		Logger::info("Action queue size before executeNextAction: " + std::to_string(_actionQueue.size()));
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
		Logger::info("executeNextAction called, queue size: " + std::to_string(_actionQueue.size()));
		
		if (_actionQueue.empty()) {
			Logger::info("Action queue empty, setting state to Idle");
			_AIstate = AIState::Idle;
			return;
		}

		auto step = _actionQueue.front();
		_actionQueue.pop();
		
		Logger::info("Executing step: " + step.toString() + ", remaining queue: " + std::to_string(_actionQueue.size()));

		int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();
		_lastCommandTime = now;
		
		std::string cmdName;

		switch (step.action) {
			case NavAction::MoveForward:
				Logger::info("Sending avance command");
				_sender.sendAvance();
				cmdName = "avance";
				break;

			case NavAction::TurnLeft:
				Logger::info("Sending gauche command");
				_sender.sendGauche();
				cmdName = "gauche";
				break;

			case NavAction::TurnRight:
				Logger::info("Sending droite command");
				_sender.sendDroite();
				cmdName = "droite";
				break;

			case NavAction::Take:
				Logger::info("Sending prend command for " + step.resource);
				_sender.sendPrend(step.resource);
				cmdName = "prend";
				break;

			case NavAction::Place:
				Logger::info("Sending pose command for " + step.resource);
				_sender.sendPose(step.resource);
				cmdName = "pose";
				break;
				
			default:
				Logger::error("Unknown action type!");
				return;
		}

		_pendingCommandId = _sender.expectResponse(cmdName,
			[this](const ServerMessage& msg) { onCommandComplete(msg); });

		Logger::debug("AI: Executing " + step.toString());
	}

	void SimpleAI::onCommandComplete(const ServerMessage& msg) {
		if (msg.status == "timeout") {
			Logger::warn("AI: Command timed out: " + msg.cmd);
			
			// Don't clear queue - retry the same action
			if (!_actionQueue.empty()) {
				// Put the step back at front
				auto step = _actionQueue.front();
				_actionQueue.pop();
				std::queue<NavigationStep> newQueue;
				newQueue.push(step);
				while (!_actionQueue.empty()) {
					newQueue.push(_actionQueue.front());
					_actionQueue.pop();
				}
				_actionQueue = newQueue;
			}
			_AIstate = AIState::Idle;
			return;
		}
		
		if (!msg.isOk()) {
			Logger::warn("AI: Command failed: " + msg.cmd);
			// Clear queue on failure
			std::queue<NavigationStep> empty;
			std::swap(_actionQueue, empty);
			_AIstate = AIState::Idle;
			return;
		}

		Logger::info("AI: Command succeeded: " + msg.cmd);
		_state.onResponse(msg);

		if (!_actionQueue.empty()) {
			executeNextAction();
		} else {
			_AIstate = AIState::Idle;
		}
	}
	
	// FIXED: Handle broadcast messages for incantation coordination
	void SimpleAI::onMessage(const ServerMessage& msg) {
		if (msg.messageText.has_value()) {
			std::string text = *msg.messageText;
			
			if (text.find("INCANT_READY") != std::string::npos) {
				// Someone is ready to incantate
				Logger::info("AI: Received incantation ready signal");
				if (_state.canIncantate() && _AIstate == AIState::Idle) {
					Logger::info("AI: Joining incantation");
					startIncantation();
				}
			}
		}
	}
} // namespace zappy