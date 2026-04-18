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
		return table[0];
	return table[level - 1];
}

static const std::vector<std::string> STONE_PRIORITY = {
	"thystame", "phiras", "mendiane", "sibur", "deraumere", "linemate"
};

Behavior::Behavior(Sender& sender, WorldState& state) : _sender(sender), _state(state) {}

// helpers
void Behavior::disbandRally(bool wasLeader) {
	_stonesReady			= false;
	_claimSent			 = false;
	_isLeader			 = false;
	_isMovingToRally	 = false;
	_isRallying			 = false;
	_peerConfirmedCount	 = 0;
	_broadcastDirection	 = -1;
	_rallyLevel			 = 0;
	_rallyBroadcastCount = 0;
	clearNavPlan();

	if (wasLeader) {
		_sender.sendDisbandLeader();
		_sender.expect("disband_leader", [this](const ServerMessage&) {
			_commandInFlight = false;
			_ignoreDone = false;
		});
		
		_ignoreDone = true; 

		_sender.sendBroadcast("DONE:" + std::to_string(_state.player.level));
		_sender.expect("broadcast", [](const ServerMessage&) {});

		_commandInFlight = true;
	}
}

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

// entrypoint -> state tick dispatcher
void Behavior::tick(int64_t nowMs) {
	if (hasCommandInFlight()) return;

	static int64_t lastInventoryRefresh = 0;
	if (nowMs - lastInventoryRefresh > 5000) {
		lastInventoryRefresh = nowMs;
		if (!isInventoryStale() && !hasCommandInFlight())
			refreshInventory();
	}

	if (isVisionStale())    { refreshVision();    return; }
	if (isInventoryStale()) { refreshInventory(); return; }

	switch (_aiState) {
		case AIState::CollectFood:      tickCollectFood();           break;
		case AIState::CollectStones:    tickCollectStones();         break;
		case AIState::Idle:             tickIdle();                  break;
		case AIState::Incantating:      tickIncantating();           break;
		case AIState::ClaimingLeader:   tickClaimingLeader();        break;
		case AIState::Leading:          tickLeading(nowMs);          break;
		case AIState::MovingToRally:    tickMovingToRally(nowMs);    break;
		case AIState::Rallying:         tickRallying(nowMs);         break;
	}
}

// refreshers
void Behavior::refreshVision() {
	_commandInFlight = true;
	_sender.sendVoir();
	_sender.expect("voir", [this](const ServerMessage& msg) {
		_commandInFlight = false;
		if (msg.vision.has_value()) {
			_state.vision = msg.vision.value();
			_staleVision = false;

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

// food collection tick
void Behavior::tickCollectFood() {
	int foodTarget = (_stonesReady) ? FOOD_RALLY : FOOD_SAFE;
	if (_state.player.inventory.nourriture >= foodTarget) {
		_stonesReady = false;
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

	if (_state.visionHasItem("nourriture")) {
		if (_navPlan.empty() || _navTarget != "nourriture") {
			clearNavPlan();
			auto tile = _state.nearestTileWithItem("nourriture");
			if (!tile.has_value()) {
				setVisionStale();
				return;
			}
			auto& t = tile.value();
			auto plan = Navigator::planPath(_state.player.orientation, t.localX, t.localY);
			_navPlan.assign(plan.begin(), plan.end());
			_navTarget = "nourriture";
		}
		if (!_navPlan.empty()) {
			NavCmd next = _navPlan.front(); _navPlan.pop_front();
			executeNavCmd(next);
		}
		return;
	}

	if (_navPlan.empty()) {
		auto plan = Navigator::explorationStep(_explorationStep);
		_navPlan.assign(plan.begin(), plan.end());
		_navTarget.clear();
	}
	if (!_navPlan.empty()) {
		NavCmd next = _navPlan.front(); _navPlan.pop_front();
		executeNavCmd(next);
	}
}

// stone gathering tick
void Behavior::tickCollectStones() {
	if (_state.player.food() < FOOD_CRITICAL) {
		_aiState = AIState::CollectFood;
		clearNavPlan();
		return;
	}

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
		const LevelReq& req = levelReq(_state.player.level);
		if (!_easyMode && req.players > 1) {
			// Require a real food buffer before starting rally —
			// both the leader (who stands still broadcasting) and the
			// follower (who walks toward the leader) need food headroom.
			if (_state.player.food() < FOOD_RALLY) {
				Logger::info("Behavior: stones ready but food too low (" +
					std::to_string(_state.player.food()) +
					"), collecting food before rally");
				_stonesReady = true;
				_aiState = AIState::CollectFood;
				return;
			}
			Logger::info("Behavior: all stones ready for level " +
				std::to_string(_state.player.level) +
				" (needs " + std::to_string(req.players) +
				" players) — claiming leadership");
			_aiState = AIState::ClaimingLeader;
			clearNavPlan();
		} else {
			_aiState = AIState::Incantating;
			_incantationReady = false;
			clearNavPlan();
		}
		return;
	}

	// pick up a needed stone that's already under our feet
	for (const auto& stone : _stonesNeeded) {
		if (_state.countItemOnCurrentTile(stone)) {
			clearNavPlan();
			_commandInFlight = true;
			_sender.sendPrend(stone);
			_sender.expect("prend " + stone, [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk()) setInventoryStale();
				setVisionStale();
			});
			return;
		}
	}

	// opportunistic food while collecting stones
	if (_state.player.food() < FOOD_SAFE && _state.countItemOnCurrentTile("nourriture")) {
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
			auto plan = Navigator::explorationStep(_explorationStep);
			_navPlan.assign(plan.begin(), plan.end());
			_navTarget.clear();
		}
	} else if (_navPlan.empty() || _navTarget != previousTarget) {
		auto plan = Navigator::planPath(_state.player.orientation, tile.localX, tile.localY);
		Logger::debug("Behavior: CollectStones: planned " + std::to_string(plan.size()) +
			" steps to " + _navTarget + " at (" +
			std::to_string(tile.localX) + "," + std::to_string(tile.localY) + ")");
		_navPlan.assign(plan.begin(), plan.end());
	}

	if (!_navPlan.empty()) {
		NavCmd next = _navPlan.front(); _navPlan.pop_front();
		executeNavCmd(next);
	}
}

// idle tick -> TODO: either give this a use or get rid of it
void Behavior::tickIdle() {
	if (_forkInProgress) return;
	_aiState = AIState::CollectStones;
}

// claming leader micro state tick
void Behavior::tickClaimingLeader() {
	// Hard latch: once we've sent claim_leader, do nothing until
	// the response callback transitions us out of this state.
	// Without this, the tick loop re-enters here on every cycle
	// because vision/inventory refreshes clear _commandInFlight
	// before the switch dispatch runs.
	if (_claimSent) return;

	_claimSent = true;
	_commandInFlight = true;

	Logger::info("Behavior: sending claim_leader for level " +
		std::to_string(_state.player.level));

	_sender.sendClaimLeader();
	_sender.expect("claim_leader", [this](const ServerMessage& msg) {
		_commandInFlight = false;
		_claimSent = false;   // reset latch regardless of outcome

		if (msg.isOk()) {
			Logger::info("Behavior: claim_leader OK — entering Leading");
			_isLeader             = true;
			_rallyLevel           = _state.player.level;
			_peerConfirmedCount   = 0;
			_lastRallyBroadcastMs = 0;   // force immediate first broadcast
			_leadingTimeoutMs     = 0;   // tickLeading init guard uses 0
			_aiState = AIState::Leading;

		} else {
			 Logger::info("Behavior: claim_leader KO — entering MovingToRally as follower");
			_isLeader        = false;
			_isMovingToRally = false;
			// Don't overwrite a direction we already cached from a RALLY broadcast
			if (_broadcastDirection <= 0 )
				_broadcastDirection = -1;  // only reset if we haven't heard RALLY yet
			_aiState = AIState::MovingToRally;
		}
	});
}

// leading tick
void Behavior::tickLeading(int64_t nowMs) {
	// One-time init (re-entrance guard via _leadingTimeoutMs == 0)
	if (_leadingTimeoutMs == 0) {
		_leadingTimeoutMs     = nowMs;
		_lastRallyBroadcastMs = nowMs - 600; // force immediate broadcast
		_rallyBroadcastCount  = 0;
		Logger::info("Behavior: Leading — level " + std::to_string(_state.player.level));
		// No return, no setVisionStale — fall through to broadcast logic below
	}

	if (nowMs - _leadingTimeoutMs >= 30000) {
		Logger::warn("Behavior: Leading timed out — disbanding");
		disbandRally(true);
		_aiState = AIState::Idle;
		return;
	}

	if (_state.player.food() < FOOD_SAFE) {
		Logger::warn("Behavior: Leading — food critical, disbanding");
		disbandRally(true);
		_aiState = AIState::CollectFood;
		return;
	}

	// Periodic RALLY broadcast — always send at least 3 before checking
	// player count, so followers have a real window to hear us even if
	// they were mid-ClaimingLeader when we first broadcast.
	if (nowMs - _lastRallyBroadcastMs >= 500) {
		_lastRallyBroadcastMs = nowMs;
		_rallyBroadcastCount++;
		_commandInFlight = true;
		_sender.sendBroadcast("RALLY:" + std::to_string(_state.player.level));
		_sender.expect("broadcast", [this](const ServerMessage&) {
			_commandInFlight = false;
		});
		return;
	}

	// Don't check player count until we've broadcast at least 3 times
	if (_rallyBroadcastCount < 3) {
		return;
	}

	if (_state.vision.empty()) {
		setVisionStale();
		return;
	}

	const auto& req = levelReq(_state.player.level);
	if (_state.vision[0].playerCount >= req.players) {
		Logger::info("Behavior: Leading — enough players (" +
			std::to_string(_state.vision[0].playerCount) + "/" +
			std::to_string(req.players) + "), moving to Rallying");
		_aiState = AIState::Rallying;
		_isRallying = false; // let tickRallying re-init
	} else {
		setVisionStale();
	}
}

// incantation tick
void Behavior::tickIncantating() {
	// Step 1: get fresh vision
	if (!_incantationReady) {
		setVisionStale();
		_incantationReady = true;
		return;
	}

	// Step 2: place required stones (one per tick)
	if (!_stonesPlaced) {
		if (_easyMode) {
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

	// Step 3: verify stones are present
	if (_staleVision) return;

	auto& tile = _state.vision[0];
	bool stonesOk;
	if (_easyMode) {
		stonesOk = tile.countItem("linemate") >= 1;
	} else {
		auto req = levelReq(_state.player.level);
		stonesOk =
			tile.countItem("linemate")  >= req.stones.linemate  &&
			tile.countItem("deraumere") >= req.stones.deraumere &&
			tile.countItem("sibur")     >= req.stones.sibur     &&
			tile.countItem("mendiane")  >= req.stones.mendiane  &&
			tile.countItem("phiras")    >= req.stones.phiras    &&
			tile.countItem("thystame")  >= req.stones.thystame;
	}

	if (!stonesOk) {
		Logger::warn("Behavior: stones missing after placement, back to CollectStones");
		_stonesPlaced = false;
		_incantationReady = false;
		_aiState = AIState::CollectStones;
		return;
	}

	// Step 4: fire incantation
	_commandInFlight = true;
	_sender.sendIncantation();
	_sender.expect("incantation", [this](const ServerMessage& msg) {
		if (msg.isInProgress()) return;

		_commandInFlight = false;
		_stonesPlaced = false;
		_incantationReady = false;

		// If we were the leader, release the server flag now
		if (_isLeader) {
			_sender.sendDisbandLeader();
			_sender.expect("disband_leader", [](const ServerMessage&) {});
			_isLeader = false;
		}

		if (_pendingLevelUp) {
			_state.player.level++;
			Logger::info("Level up! Now level " + std::to_string(_state.player.level));
			_pendingLevelUp = false;
			_aiState = (_state.player.level >= 8) ? AIState::Idle : AIState::CollectStones;
		} else {
			Logger::warn("Incantation failed (ko/timeout), restarting stone collection");
			_aiState = AIState::CollectStones;
		}
	});
}

// moving to rally tick
void Behavior::tickMovingToRally(int64_t nowMs) {
	// One-time init
	if (!_isMovingToRally) {
		_isMovingToRally = true;
		_movingToRallyTimeoutMs = nowMs;
		_navTarget.clear();  // ← add this
		clearNavPlan();
		return;
	}

	if (nowMs - _movingToRallyTimeoutMs >= 30000) {
		Logger::warn("Behavior: MovingToRally timed out");
		disbandRally(false);
		_aiState = AIState::Idle;
		return;
	}

	if (_state.player.food() < FOOD_CRITICAL) {
		Logger::warn("Behavior: MovingToRally — food critical, disbanding");
		disbandRally(false);
		_aiState = AIState::CollectFood;
		return;
	}

	// Haven't heard a RALLY broadcast yet — wait in place
	if (_broadcastDirection == -1) {
		// Haven't heard RALLY yet — don't just spin. Do opportunistic food/exploration
		// so we don't starve waiting for the leader's first broadcast.
		if (_state.player.food() < FOOD_SAFE && _state.countItemOnCurrentTile("nourriture")) {
			_commandInFlight = true;
			_sender.sendPrend("nourriture");
			_sender.expect("prend nourriture", [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk()) _state.player.inventory.nourriture++;
				setVisionStale();
			});
		} else {
			// Explore one step so we're not frozen
			auto plan = Navigator::explorationStep(_explorationStep);
			_navPlan.assign(plan.begin(), plan.end());
			if (!_navPlan.empty()) {
				NavCmd next = _navPlan.front(); _navPlan.pop_front();
				executeNavCmd(next);
			}
		}
		return;
	}

	// Already on the leader's tile
	if (_broadcastDirection == 0) {
		_isMovingToRally = false;
		_aiState = AIState::Rallying;
		setVisionStale();
		return;
	}

	// Step toward the leader using the most recent broadcast direction
	if (_navPlan.empty()) {
		auto plan = Navigator::planApproachDirection(
			_broadcastDirection, _state.player.orientation);
		_navPlan.assign(plan.begin(), plan.end());
	}

	if (!_navPlan.empty()) {
		NavCmd next = _navPlan.front(); _navPlan.pop_front();
		executeNavCmd(next);
	}
}


// rallying tick
void Behavior::tickRallying(int64_t nowMs) {
	if (!_isRallying) {
		_isRallying = true;
		_rallyingTimeoutMs = nowMs;
		setVisionStale();
		return;
	}

	if (nowMs - _rallyingTimeoutMs >= 30000) {
		Logger::warn("Behavior: Rallying timed out");
		bool wasLeader = _isLeader;
		disbandRally(wasLeader);
		_aiState = AIState::Idle;
		return;
	}

	if (!_isLeader) {
		// Follower: if direction is no longer 0, the leader moved — chase again
		if (_broadcastDirection != 0) {
			_isRallying = false;
			_isMovingToRally = false;
			_aiState = AIState::MovingToRally;
			return;
		}

		// Announce we're here
		_commandInFlight = true;
		_sender.sendBroadcast("HERE:" + std::to_string(_state.player.level));
		_sender.expect("broadcast", [this](const ServerMessage&) {
			_commandInFlight = false;
			setVisionStale();
		});
		return;
	}

	// Leader path: keep broadcasting RALLY so late-arriving followers
// can still find us. Check player count between broadcasts.
if (_state.vision.empty()) {
		setVisionStale();
		return;
	}

	const auto& req = levelReq(_state.player.level);
	if (_state.vision[0].playerCount >= req.players) {
		Logger::info("Behavior: Rallying — enough players (" +
			std::to_string(_state.vision[0].playerCount) + "/" +
			std::to_string(req.players) + "), incantating");
		_isRallying = false;
		_aiState = AIState::Incantating;
		_incantationReady = false;
		_stonesPlaced = false;
	} else {
		// Not enough players yet — keep broadcasting RALLY so followers
		// who missed Leading-phase broadcasts can still navigate here.
		if (nowMs - _lastRallyBroadcastMs >= 500) {
			_lastRallyBroadcastMs = nowMs;
			_commandInFlight = true;
			_sender.sendBroadcast("RALLY:" + std::to_string(_state.player.level));
			_sender.expect("broadcast", [this](const ServerMessage&) {
				_commandInFlight = false;
			});
		} else {
			setVisionStale();
		}
	}
}

// reaction to peer messages manager
void Behavior::onBroadcast(const ServerMessage& msg) {
	if (!msg.messageText.has_value()) return;
	const std::string& text = msg.messageText.value();
	int direction = msg.broadcastDirection.value_or(-1);

	if (text.empty()) return;

	Logger::debug("Behavior::onBroadcast: dir=" + std::to_string(direction) +
		" msg='" + text + "'");

	// ── RALLY:<level> ────────────────────────────────────────────────────
	// Leader is calling followers.  Because the server arbitrates who the
	// leader is, we no longer need to handle the "rival leader" race here.
	// If we receive RALLY and we're in ClaimingLeader or CollectStones,
	// we know the server will (or has already) answered our own claim with
	// "ko", so we can safely ignore broadcasts while in those states.
	if (text.rfind("RALLY:", 0) == 0) {
		int level = 0;
		try { level = std::stoi(text.substr(6)); } catch (...) { return; }

		if (level != _state.player.level) return;

		// We are the leader echoing back to ourselves — ignore.
		if (_isLeader) return;

		// Always store the direction — even during ClaimingLeader,
		// so if our claim comes back KO we already know where to go.
		_broadcastDirection = direction;

		// While still collecting or claiming, just cache the direction;
		// don't interrupt those states.
		if (_aiState == AIState::CollectStones ||
			_aiState == AIState::Incantating) {
			return;
		}

		if (_aiState == AIState::CollectFood) {
			// Only ignore RALLY if food is genuinely too low to participate.
			// If we have enough food, drop the food run and go rally.
			if (_state.player.food() >= FOOD_SAFE) {
				// We have enough food — respond to the leader
				_stonesReady = false;
				if (direction == 0) {
					_isMovingToRally = false;
					_isRallying = false;
					_aiState = AIState::Rallying;
				} else {
					_isMovingToRally = false;
					_aiState = AIState::MovingToRally;
				}
			}
			// else: food too low, keep eating, leader will disband and retry
			return;
		}

		// For ClaimingLeader: cache direction but don't change state —
		// the claim_leader callback will handle the transition.
		if (_aiState == AIState::ClaimingLeader) {
			return;
		}

		if (direction == 0) {
			// We're already on the leader's tile
			if (_aiState != AIState::Rallying) {
				Logger::info("Behavior: RALLY dir=0 → on leader's tile, Rallying");
				_isMovingToRally = false;
				_isRallying = false;
				_aiState = AIState::Rallying;
			}
		} else {
			// Need to move toward the leader.
			// Crucially: also re-trigger MovingToRally if we're already
			// in that state with direction==-1 (stuck waiting for first signal).
			if (_aiState != AIState::MovingToRally && _aiState != AIState::Rallying) {
				Logger::info("Behavior: RALLY dir=" + std::to_string(direction) +
					" → MovingToRally");
				_isMovingToRally = false;
				_isRallying = false;
				_aiState = AIState::MovingToRally;
			} else if (_aiState == AIState::MovingToRally && _isMovingToRally == false) {
				// Already entered state but init tick hasn't run yet — fine, direction is updated
			}
			// Direction updated above — tickMovingToRally will use it on next nav step
		}
		return;
	}

	// ── HERE:<level> ─────────────────────────────────────────────────────
	// A follower has arrived on our tile.  Only the leader cares.
	if (text.rfind("HERE:", 0) == 0) {
		int level = 0;
		try { level = std::stoi(text.substr(5)); } catch (...) { return; }

		if (!_isLeader || level != _state.player.level) return;

		_peerConfirmedCount++;
		Logger::info("Behavior: peer HERE (total=" +
			std::to_string(_peerConfirmedCount) + ")");

		const auto& req = levelReq(_state.player.level);
		if (_peerConfirmedCount >= req.players - 1) {
			Logger::info("Behavior: all peers confirmed → Rallying");
			_aiState = AIState::Rallying;
			_isRallying = false;
		}
		return;
	}

	// ── DONE:<level> ─────────────────────────────────────────────────────
	// The leader is disbanding.  Followers in rally states should go Idle
	// and look for a new leader (by going through ClaimingLeader again).
	if (text.rfind("DONE:", 0) == 0) {
		if (_isLeader || _ignoreDone) return; 
		
		int level = 0;
		try { level = std::stoi(text.substr(5)); } catch (...) { return; }

		if (level != _state.player.level) return;

		if (_aiState == AIState::MovingToRally ||
			_aiState == AIState::Rallying)
		{
			Logger::info("Behavior: DONE received — disbanding, back to CollectStones");
			disbandRally(false);
			_aiState = AIState::CollectStones;
		}
		return;
	}
}

// resource helpers
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