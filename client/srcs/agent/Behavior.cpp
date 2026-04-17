#include "Behavior.hpp"
#include "../helpers/Logger.hpp"

#include <limits>

static const LevelReq& levelReq(int level) {
	// recipe: index 0 = level 1->2, index 6 = level 7->8
	// order: players -> nourriture, linemate, deraumere, sibur, mendiane, phiras, thystame
	static const LevelReq table[7] = {
		{ 1, { 0, 1, 0, 0, 0, 0, 0 } }, // 1→2
        { 2, { 0, 1, 1, 1, 0, 0, 0 } }, // 2→3
        { 2, { 0, 2, 0, 1, 0, 2, 0 } }, // 3→4
        { 4, { 0, 1, 1, 2, 0, 1, 0 } }, // 4→5
        { 4, { 0, 1, 2, 1, 3, 0, 0 } }, // 5→6
        { 6, { 0, 1, 2, 3, 0, 1, 0 } }, // 6→7
        { 6, { 0, 2, 2, 2, 2, 2, 1 } }, // 7→8
	};

	if (level < 1 || level > 7)
		return table[0]; // never going to happen, but its a safe fallback
	return table[level - 1];
}

static const std::vector<std::string> STONE_PRIORITY = {
    "thystame", "phiras", "mendiane", "sibur", "deraumere", "linemate"
};

Behavior::Behavior(Sender& sender, WorldState& state) : _sender(sender), _state(state) {}

void Behavior::executeNavCmd(NavCmd cmd) {
	_commandInFlight = true;

	switch (cmd) {
		case NavCmd::Forward:
			_sender.sendAvance();
			_sender.expect("avance", [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk()) {
					switch (_state.player.orientation) {
						case Orientation::N: _state.player.y--; break;
						case Orientation::E: _state.player.x++; break;
						case Orientation::S: _state.player.y++; break;
						case Orientation::W: _state.player.x--; break;
						default: break;
					}
					setVisionStale();
				} else {
					clearNavPlan();
					setVisionStale();
				}
			});
			break;

		case NavCmd::TurnLeft:
			_sender.sendGauche();
			_sender.expect("gauche", [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk()) {
					switch (_state.player.orientation) {
						case Orientation::N: _state.player.orientation = Orientation::W; break;
						case Orientation::E: _state.player.orientation = Orientation::N; break;
						case Orientation::S: _state.player.orientation = Orientation::E; break;
						case Orientation::W: _state.player.orientation = Orientation::S; break;
						default: break;
					}
					setVisionStale();
				} else {
					clearNavPlan();
					setVisionStale();
				}
			});
			break;

		case NavCmd::TurnRight:
			_sender.sendDroite();
			_sender.expect("droite", [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk()) {
					switch (_state.player.orientation) {
						case Orientation::N: _state.player.orientation = Orientation::E; break;
						case Orientation::E: _state.player.orientation = Orientation::S; break;
						case Orientation::S: _state.player.orientation = Orientation::W; break;
						case Orientation::W: _state.player.orientation = Orientation::N; break;
						default: break;
					}
					setVisionStale();
				} else {
					clearNavPlan();
					setVisionStale();
				}
			});
			break;
	}
}

void Behavior::tick(int64_t nowMs) {
	if (hasCommandInFlight())	return;

	// TODO: not sure if needed
	static int64_t lastInventoryRefresh = 0;
	if (nowMs - lastInventoryRefresh > 5000) {  // Every 5 seconds
		lastInventoryRefresh = nowMs;
		if (!isInventoryStale() && !hasCommandInFlight()) {
			refreshInventory();
		}
	}

	if (isVisionStale())		{ refreshVision(); return; }
	if (isInventoryStale())		{ refreshInventory(); return; }

	switch (_aiState) {
		case AIState::CollectFood:      tickCollectFood(); break;
		case AIState::CollectStones:    tickCollectStones(); break;
		case AIState::Idle:				tickIdle(); break;
		case AIState::Incantating:      tickIncantating(); break;
		case AIState::Leading:			tickLeading(nowMs); break;
		case AIState::MovingToRally:	tickMovingToRally(nowMs); break;
		case AIState::Rallying:			tickRallying(nowMs); break;
	}
}

void Behavior::refreshVision() {
	_commandInFlight = true;
	_sender.sendVoir();
	_sender.expect("voir", [this](const ServerMessage& msg) {
		_commandInFlight = false;
		if (msg.vision.has_value()) {
			_state.vision = msg.vision.value();
			_staleVision = false;

			// if target is gone, replan
			if (!_navPlan.empty() && !_navTarget.empty() &&
				!_state.visionHasItem(_navTarget)) {
				Logger::debug("Behavior: target '" + _navTarget +
					"' no longer visible, clearing nav plan");
				clearNavPlan();
			}
		} else if (msg.isKo()) {
			Logger::warn("Voir failed");
		}
	});
}

void Behavior::refreshInventory() {
	_commandInFlight = true;
	_sender.sendInventaire();
	_sender.expect("inventaire", [this](const ServerMessage& msg) {
		_commandInFlight = false;
		if (msg.inventory.has_value()) {
			Logger::info("Refreshed inventory: " + msg.raw);
			_state.player.inventory = msg.inventory.value();
			_staleInventory = false;
		} else if (msg.isKo()) {
			Logger::warn("Inventaire failed");
		}
	});
}

void Behavior::tickCollectFood() {
	if (_state.player.inventory.nourriture >= FOOD_SAFE) {
		_aiState = AIState::CollectStones;
		clearNavPlan();
		return;
	}
	
	if (_state.countItemOnCurrentTile("nourriture")) {
		clearNavPlan();
		_commandInFlight = true;
		_sender.sendPrend("nourriture");
		_sender.expect("prend nourriture", [this](const ServerMessage& msg) {
			_commandInFlight = false;
			if (msg.isOk()) {
				_state.player.inventory.nourriture++;
				setInventoryStale();
			}
			setVisionStale();
		});
		return;
	}

	// navigate for food
	if (_state.visionHasItem("nourriture")) {
		if (_navPlan.empty() || _navTarget != "nourriture") {
			clearNavPlan();
			auto tile = _state.nearestTileWithItem("nourriture");
			if (!tile.has_value()) {
				Logger::warn("Behavior: visionHasItem true but nearestTileWithItem returned nullopt");
				setVisionStale();
				return;
			}

			auto& t = tile.value();
			std::vector<NavCmd> plan = Navigator::planPath(
				_state.player.orientation, t.localX, t.localY);

			Logger::debug("Behavior: Collect Food: planned " + std::to_string(plan.size()) +
				" steps to food at (" + std::to_string(t.localX) + "," +
				std::to_string(t.localY) + ")");

			_navPlan.assign(plan.begin(), plan.end());
			_navTarget = "nourriture";
		}

		if (!_navPlan.empty()) {
			NavCmd next = _navPlan.front();
			_navPlan.pop_front();
			executeNavCmd(next);
		}

		return;
	}

	// no food visible -> explore
	if (_navPlan.empty()) {
		std::vector<NavCmd> plan = Navigator::explorationStep(_explorationStep);
		_navPlan.assign(plan.begin(), plan.end());
		_navTarget.clear();
	}

	if (!_navPlan.empty()) {
		NavCmd next = _navPlan.front();
		_navPlan.pop_front();
		executeNavCmd(next);
	}
}

void Behavior::tickCollectStones() {
	if (_state.player.food() < FOOD_CRITICAL) {
		_aiState = AIState::CollectFood;
		clearNavPlan();
		return;
	}

	// TODO: decide if clients should have limited fork capabilities/shots or just fork whenever food is high enough
	if (_state.player.food() > FOOD_FORK && _state.player.level >= 2 && _state.forkEnabled) {
		Logger::info("Fork call triggered");
		_aiState = AIState::Idle;
		clearNavPlan();
		_commandInFlight = true;
		_sender.sendFork();
		_sender.expect("fork", [this](const ServerMessage& msg) {
			(void)msg;
			_aiState = AIState::CollectStones;
			setVisionStale();
			setInventoryStale();
			_forkInProgress = false;
			_commandInFlight = false;
		});
		_forkInProgress = true;
		return;
	}

	computeMissingStones();

	if (_stonesNeeded.empty()) {
		// if this level needs more than 1 player -> rally; else ->incantation
		const LevelReq& req = levelReq(_state.player.level);
		if (!_easyMode && req.players > 1) {
			Logger::info("Behavior: all stones collected, level " +
				std::to_string(_state.player.level) + " needs " +
				std::to_string(req.players) + " players — transitioning to Leading");
			_aiState = AIState::Leading;
			_isLeader = false;
			_peerConfirmedCount = 0;
			clearNavPlan();
		} else {
			_aiState = AIState::Incantating;
			_incantationReady = false;
			clearNavPlan();
		}
		return;
	}

	// pick up needed stone if already standing on one
	for (const auto& stone : _stonesNeeded) {
		if (_state.countItemOnCurrentTile(stone)) {
			clearNavPlan();
			_commandInFlight = true;
			_sender.sendPrend(stone);
			_sender.expect("prend " + stone, [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk())
					setInventoryStale();
				setVisionStale();
			});
			return;
		}
	}

	// Opportunistic food grab: don't interrupt a collection run, but take free food if it's right here
	if (_state.player.food() < FOOD_SAFE &&
		_state.countItemOnCurrentTile("nourriture")) {
		_commandInFlight = true;
		_sender.sendPrend("nourriture");
		_sender.expect("prend nourriture", [this](const ServerMessage& msg) {
			_commandInFlight = false;
			if (msg.isOk()) {
				_state.player.inventory.nourriture++;
				setInventoryStale();
			}
			setVisionStale();
		});
		return;
	}

	std::string previousTarget = _navTarget;
	auto tile = getNearestTileWithNeededResource();

	if (tile.localX == std::numeric_limits<int>::max()) {
		if (_navPlan.empty()) {
			std::vector<NavCmd> plan = Navigator::explorationStep(_explorationStep);
			_navPlan.assign(plan.begin(), plan.end());
			_navTarget.clear();
		}
	} else if (_navPlan.empty() || _navTarget != previousTarget) {
		std::vector<NavCmd> plan = Navigator::planPath(
			_state.player.orientation, tile.localX, tile.localY);
		Logger::debug("Behavior: CollectStones: planned " + std::to_string(plan.size()) +
			" steps to " + _navTarget + " at (" +
			std::to_string(tile.localX) + "," + std::to_string(tile.localY) + ")");
		_navPlan.assign(plan.begin(), plan.end());
	}

	if (!_navPlan.empty()) {
		NavCmd next = _navPlan.front();
		_navPlan.pop_front();
		executeNavCmd(next);
	}
}

// not doing anything really, just here for the sake of structural consistency
void Behavior::tickIdle() {
	if (_forkInProgress) return;
	_aiState = AIState::CollectStones;
	return;
}

void Behavior::tickIncantating() {
	// Step 1: Get a fresh vision first
	if (!_incantationReady) {
		setVisionStale();
		_incantationReady = true;
		return;
	}

	// Step 2: Place required stones (one per tick)
	if (!_stonesPlaced) {
		if (_easyMode) {
			// Easy mode: only need to place 1 linemate
			auto& tile = _state.vision[0];
			if (tile.countItem("linemate") < 1) {
				_commandInFlight = true;
				_sender.sendPose("linemate");
				_sender.expect("pose linemate", [this](const ServerMessage& msg) {
					_commandInFlight = false;
					if (msg.isOk()) {
						_state.player.inventory.linemate--;
						setInventoryStale();
					}
					setVisionStale();
				});
				return;
			}
		} else {
			// Normal mode: place all required stones
			auto requirements = levelReq(_state.player.level);
			auto& tile = _state.vision[0];

			#define TRY_POSE(stone_name) \
				if (requirements.stones.stone_name > 0 && \
					tile.countItem(#stone_name) < requirements.stones.stone_name) { \
					_commandInFlight = true; \
					_sender.sendPose(#stone_name); \
					_sender.expect("pose " #stone_name, [this](const ServerMessage& msg) { \
						_commandInFlight = false; \
						if (msg.isOk()) { \
							_state.player.inventory.stone_name--; \
							setInventoryStale(); \
						} \
						setVisionStale(); \
					}); \
					return; \
				}

			TRY_POSE(linemate)
			TRY_POSE(deraumere)
			TRY_POSE(sibur)
			TRY_POSE(mendiane)
			TRY_POSE(phiras)
			TRY_POSE(thystame)
			#undef TRY_POSE
		}

		_stonesPlaced = true;
		setVisionStale();
		return;
	}

	// Step 3: Verify stones - simplified for easy mode
	if (_staleVision) return;
	
	auto& tile = _state.vision[0];
	
	bool stonesOk;
	if (_easyMode) {
		stonesOk = tile.countItem("linemate") >= 1;
	} else {
		auto requirements = levelReq(_state.player.level);
		stonesOk = tile.countItem("linemate")  >= requirements.stones.linemate  &&
				tile.countItem("deraumere") >= requirements.stones.deraumere &&
				tile.countItem("sibur")     >= requirements.stones.sibur     &&
				tile.countItem("mendiane")  >= requirements.stones.mendiane  &&
				tile.countItem("phiras")    >= requirements.stones.phiras    &&
				tile.countItem("thystame")  >= requirements.stones.thystame;
	}

	if (!stonesOk) {
		Logger::warn("Behavior: stones missing on tile after placement, back to CollectStones");
		_stonesPlaced = false;
		_incantationReady = false;
		_aiState = AIState::CollectStones;
		return;
	}

	// Step 4: Send incantation
	_commandInFlight = true;
	_sender.sendIncantation();
	_sender.expect("incantation", [this](const ServerMessage& msg) {
		if (msg.isInProgress()) {
			return;
		}

		_commandInFlight = false;
		_stonesPlaced = false;
		_incantationReady = false;

		if (_pendingLevelUp) {
			_state.player.level++;
			Logger::info("Level up! Now level " + std::to_string(_state.player.level));
			_pendingLevelUp = false;
			_aiState = (_state.player.level >= 8) ? AIState::Idle : AIState::CollectStones;
		} else {
			Logger::warn("Incantation failed (ko or timeout), restarting stone collection");
			_aiState = AIState::CollectStones;
		}
	});
}

void Behavior::tickLeading(int64_t nowMs) {
	// initialise leader state
	if (!_isLeader) {
		setVisionStale();
		_isLeader = true;
		_rallyLevel = _state.player.level;
		_lastRallyBroadcastMs = nowMs;
		_leadingTimeoutMs = nowMs;
		_peerConfirmedCount = 0;
		Logger::info("Behavior: becoming leader for level " + std::to_string(_rallyLevel));
		return;
	}

	// Timeout
	if (nowMs - _leadingTimeoutMs >= 30000) {
		Logger::warn("Behavior: leading timed out, disbanding rally");
		_isLeader = false;
		_rallyLevel = 0;
		_lastRallyBroadcastMs = 0;
		_leadingTimeoutMs = 0;
		_peerConfirmedCount = 0;
		_sender.sendBroadcast("DONE:" + std::to_string(_state.player.level));
		_sender.expect("broadcast", [this](const ServerMessage&) {
			_commandInFlight = false;
		});
		_commandInFlight = true;
		_aiState = AIState::Idle;
		return;
	}

	// Broadcast RALLY every 500ms to attract followers
	if (nowMs - _lastRallyBroadcastMs >= 500) {
		_lastRallyBroadcastMs = nowMs;
		_sender.sendBroadcast("RALLY:" + std::to_string(_state.player.level));
		_sender.expect("broadcast", [this](const ServerMessage&) {
			_commandInFlight = false;
		});
		_commandInFlight = true;
		return;
	}

	// this guard should not be needed but at ths point I'm scared
	if (_state.vision.empty()) {
		setVisionStale();
		return;
	}

	// Check if enough same level little dudes in same tile
	const auto& requirements = levelReq(_state.player.level);
	if (_state.vision[0].playerCount >= requirements.players) {
		Logger::info("Behavior: enough players on tile (" +
			std::to_string(_state.vision[0].playerCount) + "/" +
			std::to_string(requirements.players) + "), moving to Rallying");
		_aiState = AIState::Rallying;
		return;
	}

	// Not enough yet, so rerefsh vision
	setVisionStale();
}

void Behavior::tickMovingToRally(int64_t nowMs) {
	if (!_isMovingToRally) {
		_isMovingToRally = true;
		_movingToRallyTimeoutMs = nowMs;
		setVisionStale();
		return;
	}

	if (nowMs - _movingToRallyTimeoutMs >= 30000) {
		Logger::warn("Behavior: MovingToRally timed out");
		_isMovingToRally = false;
		_movingToRallyTimeoutMs = 0;
		_aiState = AIState::Idle;
		return;
	}

	if (_state.player.food() < FOOD_CRITICAL) {
		Logger::warn("Behavior: MovingToRally - food critical, disbanding");
		_isMovingToRally = false;
		_aiState = AIState::CollectFood;
		clearNavPlan();
		return;
	}

	if (_broadcastDirection == 0) {
		_isMovingToRally = false;
		_aiState = AIState::Rallying;
		return;
	}

	// Only build a new plan when the current one is exhausted
	if (_navPlan.empty()) {
		std::vector<NavCmd> plan = Navigator::planApproachDirection(
			_broadcastDirection, _state.player.orientation);
		_navPlan.assign(plan.begin(), plan.end());
	}

	if (!_navPlan.empty()) {
		NavCmd next = _navPlan.front();
		_navPlan.pop_front();
		executeNavCmd(next);
	}
}

void Behavior::tickRallying(int64_t nowMs) {
	// init
	if (!_isRallying) {
		_isRallying = true;
		_rallyingTimeoutMs = nowMs;
		setVisionStale();
		return;
	}

	if (nowMs - _rallyingTimeoutMs >= 30000) {
		Logger::warn("Behavior: Rallying timed out");
		_isRallying = false;
		_rallyingTimeoutMs = 0;
		_isLeader = false;
		_aiState = AIState::Idle;
		return;
	}

	// Follower: if on the leader's tile (direction == 0), announce presence
	if (!_isLeader) {
		if (_broadcastDirection == 0) {
			_commandInFlight = true;
			_sender.sendBroadcast("HERE:" + std::to_string(_state.player.level));
			_sender.expect("broadcast", [this](const ServerMessage& msg) {
				(void)msg;
				_commandInFlight = false;
				setVisionStale();
			});
		} else {
			// Leader moved or we drifted -> go back to MovingToRally
			_isRallying = false;
			_isMovingToRally = false;
			_aiState = AIState::MovingToRally;
		}
		return;
	}

	// Leader path: guard against empty vision
	if (_state.vision.empty()) {
		setVisionStale();
		return;
	}

	const auto& requirements = levelReq(_state.player.level);

	// FIXED: >= means we have enough players, so incantate
	if (_state.vision[0].playerCount >= requirements.players) {
		Logger::info("Behavior: leader has enough players (" +
			std::to_string(_state.vision[0].playerCount) + "/" +
			std::to_string(requirements.players) + "), starting incantation");
		_isRallying = false;
		_isLeader = false;
		_peerConfirmedCount = 0;
		_aiState = AIState::Incantating;
	} else {
		// Not enough yet, keep watching
		setVisionStale();
	}
}

void Behavior::onBroadcast(const ServerMessage& msg) {
	const std::string& text = msg.messageText.value();
	int direction = msg.broadcastDirection.value_or(-1);

	if (text.empty()) {
		Logger::debug("Behavior::onBroadcast: empty message, ignoring");
		return;
	}

	Logger::debug("Behavior::onBroadcast: dir=" + std::to_string(direction) +
		" msg='" + text + "'");

	// RALLY:<level>
	// A leader is broadcasting its position. Followers navigate toward it;
	// if we are the leader and the signal comes from elsewhere (direction != 0),
	// it means another leader exists -> become follower
	if (text.rfind("RALLY:", 0) == 0) {
		int level = 0;
		try { level = std::stoi(text.substr(6)); } catch (...) { return; }

		if (level != _state.player.level)
			return; // different-level rally, not relevant

		if (_isLeader) {
			if (direction != 0) {
				// Another leader exists on a different tile -> demoted to follower
				// Whichever broadcasts last "loses"
				Logger::info("Behavior: rival RALLY detected (dir=" +
					std::to_string(direction) + "), relinquishing leadership");
				_isLeader = false;
				_peerConfirmedCount = 0;
				_sender.sendBroadcast("DONE:" + std::to_string(_state.player.level));
				_sender.expect("broadcast", [this](const ServerMessage&) {
					_commandInFlight = false;
				});
				_commandInFlight = true;
				_broadcastDirection = direction;
				_isMovingToRally = false;
				_isRallying = false;
				_aiState = AIState::MovingToRally;
			}
			// direction == 0 means a follower is already on our tile, so nothing to do
			return;
		}

		// We are a follower: update direction and (re)enter the approach state
		_broadcastDirection = direction;
		if (direction == 0) {
			// We are already on the leader's tile
			if (_aiState != AIState::Rallying) {
				Logger::info("Behavior: RALLY dir=0, already on leader's tile → Rallying");
				_isMovingToRally = false;
				_isRallying = false;
				_aiState = AIState::Rallying;
			}
		} else {
			if (_aiState != AIState::MovingToRally && _aiState != AIState::Rallying) {
				Logger::info("Behavior: RALLY received (dir=" + std::to_string(direction) +
					") → MovingToRally");
				_isMovingToRally = false;
				_isRallying = false;
				_aiState = AIState::MovingToRally;
			}
			// If already MovingToRally, just update _broadcastDirection so the
			// next tickMovingToRally uses the freshest bearing. TODO?
		}
		return;
	}

	// HERE:<level> 
	// A follower has arrived on our tile. Only the leader cares.
	if (text.rfind("HERE:", 0) == 0) {
		int level = 0;
		try { level = std::stoi(text.substr(5)); } catch (...) { return; }

		if (!_isLeader || level != _state.player.level)
			return;

		_peerConfirmedCount++;
		Logger::info("Behavior: peer confirmed HERE (total=" +
			std::to_string(_peerConfirmedCount) + ")");

		const auto& req = levelReq(_state.player.level);
		if (_peerConfirmedCount >= req.players - 1) {
			// -1 because the leader itself is one of the required players
			Logger::info("Behavior: all peers confirmed, transitioning to Rallying");
			_aiState = AIState::Rallying;
			_isRallying = false; // let tickRallying re-initialise
		}
		return;
	}

	// DONE:<level>
	// The leader has given up. Everyone in rally states should disband.
	if (text.rfind("DONE:", 0) == 0) {
		int level = 0;
		try { level = std::stoi(text.substr(5)); } catch (...) { return; }

		if (level != _state.player.level)
			return;

		if (_aiState == AIState::MovingToRally ||
			_aiState == AIState::Rallying ||
			_aiState == AIState::Leading) {
			Logger::info("Behavior: DONE received, disbanding rally → Idle");
			_isLeader = false;
			_isMovingToRally = false;
			_isRallying = false;
			_peerConfirmedCount = 0;
			_broadcastDirection = 0;
			_aiState = AIState::Idle;
		}
		return;
	}

	// START:<level>
	// (Reserved for future use — leader signals incantation is starting so
	//  followers know to stay put.)
	if (text.rfind("START:", 0) == 0) {
		Logger::debug("Behavior: START broadcast received (not yet handled)");
		return;
	}
}

void Behavior::computeMissingStones() {
	if (_state.vision.empty()) return;

	_stonesNeeded.clear();

	if (_easyMode) {
		if (_state.player.inventory.linemate < 1)
			_stonesNeeded.push_back("linemate");
		return;
	}

	const LevelReq& req = levelReq(_state.player.level);
	Inventory& inv = _state.player.inventory;

	for (const auto& stone : STONE_PRIORITY) {
		if      (stone == "linemate"  && req.stones.linemate  > inv.linemate)  _stonesNeeded.push_back(stone);
		else if (stone == "deraumere" && req.stones.deraumere > inv.deraumere) _stonesNeeded.push_back(stone);
		else if (stone == "sibur"     && req.stones.sibur     > inv.sibur)     _stonesNeeded.push_back(stone);
		else if (stone == "mendiane"  && req.stones.mendiane  > inv.mendiane)  _stonesNeeded.push_back(stone);
		else if (stone == "phiras"    && req.stones.phiras    > inv.phiras)    _stonesNeeded.push_back(stone);
		else if (stone == "thystame"  && req.stones.thystame  > inv.thystame)  _stonesNeeded.push_back(stone);
	}
}

VisionTile Behavior::getNearestTileWithNeededResource() {
	VisionTile nearest;
	nearest.localX = std::numeric_limits<int>::max();
	nearest.localY = std::numeric_limits<int>::max();

	for (const auto& stone : _stonesNeeded) {
		auto tile = _state.nearestTileWithItem(stone);
		if (tile.has_value()) {
			nearest = tile.value();
			_navTarget = stone;
			return nearest;
		}
	}

	return nearest;
}
