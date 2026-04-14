#include "Behavior.hpp"
#include "../helpers/Logger.hpp"

Behavior::Behavior(Sender& sender, WorldState& state) : _sender(sender), _state(state) {}

// Executes a single NavCmd by sending the appropriate command and registering
// a callback that clears commandInFlight and marks vision stale on success.
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
					_navPlan.clear();
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
					_navPlan.clear();
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
					_navPlan.clear();
					setVisionStale();
				}
			});
			break;
	}
}

void Behavior::tick(int64_t nowMs) {
	(void)nowMs;

	if (hasCommandInFlight()) return;

	// --- Refresh vision ---
	if (isVisionStale()) {
		_commandInFlight = true;
		_sender.sendVoir();
		_sender.expect("voir", [this](const ServerMessage& msg) {
			_commandInFlight = false;
			if (msg.vision.has_value()) {
				_state.vision = msg.vision.value();
				_staleVision = false;

				// If we have an active nav plan, verify the target is still visible.
				// If not, scrap the plan and replan next tick.
				if (!_navPlan.empty() && !_state.visionHasItem("nourriture")) {
					Logger::debug("Behavior: target no longer visible, clearing nav plan");
					_navPlan.clear();
				}
			} else if (msg.isKo()) {
				Logger::warn("Voir failed");
			}
		});
		return;
	}

	// --- Refresh inventory ---
	if (isInventoryStale()) {
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
		return;
	}

	// --- Pick up food if standing on it ---
	if (_state.countItemOnCurrentTile("nourriture")) {
		_navPlan.clear(); // already here, no need to navigate
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

	// --- Navigate toward food ---
	if (_state.visionHasItem("nourriture")) {
		// Build a plan if we don't have one
		if (_navPlan.empty()) {
			auto tile = _state.nearestTileWithItem("nourriture");
			if (!tile.has_value()) {
				// Shouldn't happen (visionHasItem is true), but guard it
				Logger::warn("Behavior: visionHasItem true but nearestTileWithItem returned nullopt");
				setVisionStale();
				return;
			}

			auto& t = tile.value();
			std::vector<NavCmd> plan = Navigator::planPath(
				_state.player.orientation, t.localX, t.localY);

			Logger::debug("Behavior: planned " + std::to_string(plan.size()) +
				" steps to food at (" + std::to_string(t.localX) + "," +
				std::to_string(t.localY) + ")");

			_navPlan.assign(plan.begin(), plan.end());
		}

		// Execute the next step
		if (!_navPlan.empty()) {
			NavCmd next = _navPlan.front();
			_navPlan.pop_front();
			executeNavCmd(next);
		}
		return;
	}

	// --- Explore ---
	// No food visible: run an exploration step
	if (_navPlan.empty()) {
		std::vector<NavCmd> plan = Navigator::explorationStep(_explorationStep);
		_navPlan.assign(plan.begin(), plan.end());
	}

	if (!_navPlan.empty()) {
		NavCmd next = _navPlan.front();
		_navPlan.pop_front();
		executeNavCmd(next);
	}
}

void Behavior::onResponse(const ServerMessage& msg) {
	// TODO: handle broadcast responses in later steps
	(void)msg;
}
