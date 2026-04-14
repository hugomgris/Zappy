#include "Behavior.hpp"
#include "../helpers/Logger.hpp"

Behavior::Behavior(Sender& sender, WorldState& state) : _sender(sender), _state(state) {}

void Behavior::tick(int64_t nowMs) {
	// TODO: remove this when using nowms
	(void)nowMs;

	if (hasCommandInFlight()) return;

	if (isVisionStale()) {
		_commandInFlight = true;
		_sender.sendVoir();
		_sender.expect("voir", [this](const ServerMessage& msg) {
			_commandInFlight = false;
			if (msg.vision.has_value()) {
				_state.vision = msg.vision.value();
				_staleVision = false;
			} else if (msg.isKo()) {
				Logger::warn("Voir failed");
			}
		});
		return;
	}

	if (isInventoryStale()) {
		_commandInFlight = true;
		_sender.sendInventaire();
		_sender.expect("inventaire", [this](const ServerMessage& msg) {
			_commandInFlight = false;
			if (msg.inventory.has_value()) {
				Logger::info("Refreshed inventory from msg raw: " + msg.raw);
				_state.player.inventory = msg.inventory.value();
				_staleInventory = false;
			} else if (msg.isKo()) {
				Logger::warn("Inventaire failed");
			}
		});
		return;
	}

	if (_state.countItemOnCurrentTile("nourriture")) {
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
	} else if (_state.visionHasItem("nourriture")) {
		_commandInFlight = true;
		auto tile = _state.nearestTileWithItem("nourriture");
		if (!tile.has_value()) {
			Logger::warn("Behavior: visionHasItem true but nearestTileWithItem returned nullopt");
			_commandInFlight = false;
			return;
		}

		auto value = tile.value();
		if (value.localX < 0) {
			_sender.sendGauche();
			_sender.expect("gauche", [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk()) {
					switch (_state.player.orientation) {
						case Orientation::N: _state.player.orientation = Orientation::W; break;
						case Orientation::E: _state.player.orientation = Orientation::N; break;
						case Orientation::S: _state.player.orientation = Orientation::E; break;
						case Orientation::W: _state.player.orientation = Orientation::S; break;
						default: _state.player.orientation = Orientation::W; break;
					}
					setVisionStale();
				}
			});
		} else if (value.localX > 0) {
			_sender.sendDroite();
			_sender.expect("droite", [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk()) {
					switch (_state.player.orientation) {
						case Orientation::N: _state.player.orientation = Orientation::E; break;
						case Orientation::E: _state.player.orientation = Orientation::S; break;
						case Orientation::S: _state.player.orientation = Orientation::W; break;
						case Orientation::W: _state.player.orientation = Orientation::N; break;
						default: _state.player.orientation = Orientation::E; break;
					}
					setVisionStale();
				}
			});
		} else {
			_sender.sendAvance();
			_sender.expect("avance", [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk()) {
					switch (_state.player.orientation) {
						// space loop handled by navigator
						case Orientation::N:
							_state.player.y--;
							break;
						case Orientation::E:
							_state.player.x++;
							break;
						case Orientation::S:
							_state.player.y++;
							break;
						case Orientation::W: 
							_state.player.x--;
							break;
						default:
							_state.player.y--;
							break;
					}
					setVisionStale();
				}
			});
		}
	} else {
		// placeholded until Navigator is set up
		_commandInFlight = true;
		_sender.sendDroite();
		_sender.expect("droite", [this](const ServerMessage& msg) {
			_commandInFlight = false;
			if (msg.isOk()) {
				switch (_state.player.orientation) {
					case Orientation::N: _state.player.orientation = Orientation::E; break;
					case Orientation::E: _state.player.orientation = Orientation::S; break;
					case Orientation::S: _state.player.orientation = Orientation::W; break;
					case Orientation::W: _state.player.orientation = Orientation::N; break;
					default: _state.player.orientation = Orientation::E; break;
				}
				setVisionStale();
			}
		});
	}
}

void Behavior::onResponse(const ServerMessage& msg) {
    // TODO: handle broadcast responses in later steps
    (void)msg;
}