#include "Behavior.hpp"
#include "../helpers/Logger.hpp"

#include <limits>

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
	(void)nowMs;

	if (hasCommandInFlight())	return;
	if (isVisionStale())		{ refreshVision(); return; }
	if (isInventoryStale())		{ refreshInventory(); return; }

	switch (_aiState) {
		case AIState::CollectFood:      tickCollectFood(); break;
		case AIState::CollectStones:    tickCollectStones(); break;
		case AIState::Incantating:      tickIncantating(); break;
		case AIState::Idle:             break;
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

			// if target is gone, sreplan
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

	// no food visible->explore
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

    computeMissingStones();

    if (_stonesNeeded.empty()) {
        _aiState = AIState::Incantating;
        clearNavPlan();
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

    // navigate towards nearest needed resource
    clearNavPlan();
    auto tile = getNearestTileWithNeededResource();

    if (tile.localX == std::numeric_limits<int>::max()) {
        // nothing visible yet->explore
        std::vector<NavCmd> plan = Navigator::explorationStep(_explorationStep);
        _navPlan.assign(plan.begin(), plan.end());
        _navTarget.clear();
    } else {
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

void Behavior::tickIncantating() {

}

void Behavior::onResponse(const ServerMessage& msg) {
	// TODO: handle broadcast responses in later steps
	// TODO: rename to onBroadcast?
	(void)msg;
}

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

void Behavior::computeMissingStones() {
	if (_state.vision.empty()) return;
	
	int currentLevel = _state.player.level;
	Inventory currentStones = _state.player.inventory;
	VisionTile currentTile = _state.vision[0];

	const LevelReq& requirements = levelReq(currentLevel);
	_stonesNeeded.clear();
	
	// TODO: manage the _stonesNeeded attribute as an Inventory type?
	if (requirements.stones.linemate > currentStones.linemate + currentTile.countItem("linemate")) {
		_stonesNeeded.push_back("linemate");
	}
	if (requirements.stones.deraumere > currentStones.deraumere + currentTile.countItem("deraumere")) {
		_stonesNeeded.push_back("deraumere");
	}
	if (requirements.stones.sibur > currentStones.sibur + currentTile.countItem("sibur")) {
		_stonesNeeded.push_back("sibur");
	}
	if (requirements.stones.mendiane > currentStones.mendiane + currentTile.countItem("mendiane")) {
		_stonesNeeded.push_back("mendiane");
	}
	if (requirements.stones.phiras > currentStones.phiras + currentTile.countItem("phiras")) {
		_stonesNeeded.push_back("phiras");
	}
	if (requirements.stones.thystame > currentStones.thystame + currentTile.countItem("thystame")) {
		_stonesNeeded.push_back("thystame");
	}
}

VisionTile Behavior::getNearestTileWithNeededResource() {
	VisionTile nearest;
	nearest.localX = std::numeric_limits<int>::max();
	nearest.localY = std::numeric_limits<int>::max();

	for (size_t i = 0; i < _stonesNeeded.size(); ++i) {
		auto tile = _state.nearestTileWithItem(_stonesNeeded[i]);
		if (tile.has_value()) {
			if ((tile.value().localY + std::abs(tile.value().localX)) < (nearest.localY + nearest.localX)) {
				nearest = tile.value();
				_navTarget = _stonesNeeded[i];
			}
		}
	}

	return nearest;
}
