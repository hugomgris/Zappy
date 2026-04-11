#include "WorldState.hpp"
#include "helpers/Logger.hpp"
#include <algorithm>

namespace zappy {
	WorldState::WorldState() {
		_player.inventory["nourriture"] = 10;
	}

	void WorldState::onWelcome(const ServerMessage& msg) {
		std::lock_guard<std::recursive_mutex> lock(_mutex);

		_connected = true;
		if (msg.mapSize.has_value()) {
			_mapSize = msg.mapSize;
			Logger::info("Map size: " + std::to_string(_mapSize->x) + "x" + std::to_string(_mapSize->y));
		}
		if (msg.remainingClients.has_value()) {
			_player.remainingSlots = *msg.remainingClients;
			Logger::info("Remaining team slots: " + std::to_string(_player.remainingSlots));
		}
	}

	void WorldState::onResponse(const ServerMessage& msg) {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		
		// TEMPORARY DEBUG: Log all responses to see what's coming in
		Logger::info("=== onResponse received ===");
		Logger::info("  cmd: " + msg.cmd);
		Logger::info("  status: " + msg.status);
		Logger::info("  arg: " + msg.arg);
		Logger::info("  raw: " + msg.raw);
		
		if (msg.cmd == "voir") {
			Logger::info("=== VOIR RESPONSE DETECTED ===");
			Logger::info("  vision.has_value(): " + std::to_string(msg.vision.has_value()));
			if (msg.vision.has_value()) {
				Logger::info("  vision size: " + std::to_string(msg.vision->size()));
				// Log first few tiles to see what we got
				for (size_t i = 0; i < std::min(size_t(5), msg.vision->size()); i++) {
					const auto& tile = (*msg.vision)[i];
					Logger::info("    Tile[" + std::to_string(i) + "]: distance=" + std::to_string(tile.distance) +
							", items=" + std::to_string(tile.items.size()) +
							", players=" + std::to_string(tile.playerCount));
					if (!tile.items.empty()) {
						std::string itemsStr;
						for (const auto& item : tile.items) {
							if (!itemsStr.empty()) itemsStr += ", ";
							itemsStr += item;
						}
						Logger::info("      Items: " + itemsStr);
					}
				}
			} else {
				Logger::error("=== VOIR RESPONSE HAS NO VISION DATA! ===");
				Logger::error("  This means ProtocolTypes::parseServerMessage failed to parse the vision array");
			}
			
			if (msg.vision.has_value()) {
				updateVision(*msg.vision);
			} else {
				Logger::warn("  Skipping updateVision because vision has no value");
			}
		}
		else if (msg.cmd == "inventaire" && msg.inventory.has_value()) {
			Logger::info("=== INVENTAIRE RESPONSE ===");
			std::string invStr;
			for (const auto& [item, count] : *msg.inventory) {
				if (!invStr.empty()) invStr += ", ";
				invStr += item + "=" + std::to_string(count);
			}
			Logger::info("  Inventory: " + invStr);
			updateInventory(*msg.inventory);
		}
		else if (msg.cmd == "avance" && msg.isOk()) {
			if (_mapSize.has_value()) {
				int oldX = _player.x;
				int oldY = _player.y;
				applyMove(_player.x, _player.y, _player.orientation, _mapSize->x, _mapSize->y);
                                _vision.clear();
				Logger::info("=== AVANCE ===");
				Logger::info("  Moved from (" + std::to_string(oldX) + "," + std::to_string(oldY) + 
							") to (" + std::to_string(_player.x) + "," + std::to_string(_player.y) + ")");
			}
		}
		else if (msg.cmd == "droite" && msg.isOk()) {
			int oldOrientation = _player.orientation;
			applyTurn(_player.orientation, true);
                                _vision.clear();
			Logger::info("=== DROITE ===");
			Logger::info("  Turned from " + orientationToString(oldOrientation) + 
						" to " + orientationToString(_player.orientation));
		}
		else if (msg.cmd == "gauche" && msg.isOk()) {
			int oldOrientation = _player.orientation;
			applyTurn(_player.orientation, false);
                                _vision.clear();
			Logger::info("=== GAUCHE ===");
			Logger::info("  Turned from " + orientationToString(oldOrientation) + 
						" to " + orientationToString(_player.orientation));
		}
		else if (msg.cmd == "fork" && msg.isOk()) {
			_forkCount++;
			Logger::info("=== FORK ===");
			Logger::info("  Fork successful! Total forks: " + std::to_string(_forkCount));
		}
		else if (msg.cmd == "connect_nbr") {
			try {
				_player.remainingSlots = std::stoi(msg.arg);
				Logger::info("=== CONNECT_NBR ===");
				Logger::info("  Team slots available: " + std::to_string(_player.remainingSlots));
			} catch (...) {
				Logger::error("  Failed to parse connect_nbr arg: " + msg.arg);
			}
		}
		else if (msg.cmd == "incantation") {
			Logger::info("=== INCANTATION ===");
			if (msg.arg == "in_progress") {
				Logger::info("  Incantation in progress...");
			} else if (msg.isOk()) {
				Logger::info("  Incantation completed!");
			} else if (msg.isKo()) {
						Logger::info("  Incantation failed");
					} else {
				Logger::info("  Incantation response: arg=" + msg.arg + ", status=" + msg.status);
			}
		}
		else if (msg.cmd == "deplacement" && msg.direction.has_value()) {
			Logger::info("=== DEPLACEMENT (Expulse) ===");
			Logger::info("  Expulsed! New direction relative: " + std::to_string(*msg.direction));
		}
		else if (msg.cmd == "prend") {
			Logger::info("=== PREND ===");
			Logger::info("  Arg: " + msg.arg + ", Status: " + msg.status);
			if (msg.isOk()) {
				// Update inventory when take succeeds
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_player.inventory[msg.arg]++;
				if (msg.arg == "nourriture") {
					Logger::info("  Food increased to " + std::to_string(_player.inventory["nourriture"]));
				}
				if (!_vision.empty()) {
					auto it = std::find(_vision[0].items.begin(), _vision[0].items.end(), msg.arg);
					if (it != _vision[0].items.end()) {
						_vision[0].items.erase(it);
					}
				}
			} else if (msg.isKo()) { 
				Logger::error("  PREND FAILED! Erasing from vision.");
				if (!_vision.empty()) {
					_vision[0].items.erase(
						std::remove(_vision[0].items.begin(), _vision[0].items.end(), msg.arg),
						_vision[0].items.end()
					);
				}
				Logger::error("  PREND FAILED! Resource not available?");
			}
		}
		else if (msg.cmd == "pose") {
			Logger::info("=== POSE ===");
			Logger::info("  Arg: " + msg.arg + ", Status: " + msg.status);
			if (msg.isOk()) {
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				if (_player.inventory[msg.arg] > 0) {
					_player.inventory[msg.arg]--;
				}
				if (!_vision.empty()) {
					_vision[0].items.push_back(msg.arg);
				}
			} else if (msg.isKo()) { 
							Logger::error("  PREND FAILED! Erasing from vision.");
							if (!_vision.empty()) {
								auto it = std::find(_vision[0].items.begin(), _vision[0].items.end(), msg.arg);
								if (it != _vision[0].items.end()) _vision[0].items.erase(it);
							}

				Logger::error("  POSE FAILED! Resource not in inventory?");
			}
		}
		else if (msg.cmd == "broadcast") {
			Logger::info("=== BROADCAST ===");
			Logger::info("  Status: " + msg.status);
		}
		else {
			Logger::info("=== UNHANDLED RESPONSE ===");
			Logger::info("  cmd: " + msg.cmd);
			Logger::info("  raw: " + msg.raw);
		}
	}

	void WorldState::onEvent(const ServerMessage& msg) {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		
		if (msg.isLevelUp()) {
			_player.level++;
			_levelUpCount++;
			Logger::info("LEVEL UP! Now level " + std::to_string(_player.level));
		}
		else if (msg.isDeath()) {
			_connected = false;
			Logger::error("Player died!");
		}
	}

	void WorldState::onMessage(const ServerMessage& msg) {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		
		if (msg.messageText.has_value() && msg.direction.has_value()) {
			Logger::info("Broadcast from dir " + std::to_string(*msg.direction) + ": " + *msg.messageText);
		}
	}

	void WorldState::updateInventory(const std::map<std::string, int>& inv) {
		// _mutex is already locked in onResponse if called from there, 
        // but can be called from outside too. To prevent deadlock we only lock if needed,
        // or just use a std::unique_lock or std::recursive_mutex.
        // Actually, let's just make the lock a recursive_mutex in the header.
        std::lock_guard<std::recursive_mutex> lock(_mutex);
		// REPLACE completely - don't merge
		_player.inventory.clear();
		_player.inventory = inv;
		
		_lastInventoryTime = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		
		// Debug logging
		std::string invStr;
		for (const auto& [item, count] : _player.inventory) {
			if (!invStr.empty()) invStr += ", ";
			invStr += item + "=" + std::to_string(count);
		}
		Logger::debug("Inventory updated (REPLACE): " + invStr);
	}

	void WorldState::updateVision(const std::vector<VisionTile>& vision) {
		_vision = vision;
		_visionHistory.push_back(vision);  // FIXED: Keep history
		if (_visionHistory.size() > 10) {
			_visionHistory.pop_front();
		}
		
		_lastVisionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();
		
		Logger::debug("Vision updated: " + std::to_string(_vision.size()) + " tiles");
	}
	
	int WorldState::getPlayersOnTile() const {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		if (_vision.empty()) return 0;
		return _vision[0].playerCount;
	}

	bool WorldState::seesItem(const std::string& item) const {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		for (const auto& tile : _vision) {
			if (tile.hasItem(item)) return true;
		}
		return false;
	}

	std::optional<VisionTile> WorldState::getNearestItem(const std::string& item) const {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		for (const auto& tile : _vision) {
			if (tile.hasItem(item)) return tile;
		}
		return std::nullopt;
	}

	std::vector<VisionTile> WorldState::getTilesWithItem(const std::string& item) const {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		std::vector<VisionTile> result;
		for (const auto& tile : _vision) {
			if (tile.hasItem(item)) result.push_back(tile);
		}
		return result;
	}

	LevelRequirement WorldState::getLevelRequirement(int level) const {
		static const std::map<int, LevelRequirement> requirements = {
			{1, {1, {{"linemate", 1}}}},
			{2, {2, {{"linemate", 1}, {"deraumere", 1}, {"sibur", 1}}}},
			{3, {2, {{"linemate", 2}, {"sibur", 1}, {"phiras", 2}}}},
			{4, {4, {{"linemate", 1}, {"deraumere", 1}, {"sibur", 2}, {"phiras", 1}}}},
			{5, {4, {{"linemate", 1}, {"deraumere", 2}, {"sibur", 1}, {"mendiane", 3}}}},
			{6, {6, {{"linemate", 1}, {"deraumere", 2}, {"sibur", 3}, {"phiras", 1}}}},
			{7, {6, {{"linemate", 2}, {"deraumere", 2}, {"sibur", 2}, {"mendiane", 2}, {"phiras", 2}, {"thystame", 1}}}}
		};
		
		auto it = requirements.find(level);
		if (it != requirements.end()) return it->second;
		return {1, {}};
	}

	bool WorldState::hasStonesForIncantation() const {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		if (_player.level >= 8) return false;

		auto req = getLevelRequirement(_player.level);
		std::map<std::string, int> availableOnTile;
		if (!_vision.empty()) {
			for (const auto& item : _vision[0].items) {
				availableOnTile[item]++;
			}
		}

		for (const auto& [stone, needed] : req.stonesNeeded) {
			auto it = _player.inventory.find(stone);
			int have = (it != _player.inventory.end()) ? it->second : 0;
			int onTile = availableOnTile[stone];
			
			if (have + onTile < needed) {
				return false;
			}
		}
		return true;
	}

	bool WorldState::canIncantate() const {
		std::lock_guard<std::recursive_mutex> lock(_mutex);

		if (_player.level >= 8) return false;

		auto req = getLevelRequirement(_player.level);

		// check players on tile
		int playersOnTile = _vision.empty() ? 0 : _vision[0].playerCount;
		if (playersOnTile < req.playersNeeded) return false;

		return hasStonesForIncantation();
	}

	std::vector<std::string> WorldState::getMissingStones() const {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		std::vector<std::string> missing;
		
		if (_player.level >= 8) return missing;
		
		Logger::debug("getMissingStones: level=" + std::to_string(_player.level) + 
					", vision empty=" + std::to_string(_vision.empty()));
		
		// Check stones on current tile
		std::map<std::string, int> availableOnTile;
		if (!_vision.empty()) {
			for (const auto& item : _vision[0].items) {
				availableOnTile[item]++;
				Logger::debug("Tile has: " + item);
			}
		}
		
		auto req = getLevelRequirement(_player.level);
		for (const auto& [stone, needed] : req.stonesNeeded) {
			auto it = _player.inventory.find(stone);
			int have = (it != _player.inventory.end()) ? it->second : 0;
			int onTile = availableOnTile[stone];
			
			Logger::debug("Stone " + stone + ": have=" + std::to_string(have) + 
						", onTile=" + std::to_string(onTile) + 
						", needed=" + std::to_string(needed));
			
			int totalNeeded = needed - onTile;
			if (totalNeeded < 0) totalNeeded = 0;
			
			if (have < totalNeeded) {
				for (int i = 0; i < totalNeeded - have; i++) {
					missing.push_back(stone);
				}
			}
		}
		
		Logger::debug("Missing stones count: " + std::to_string(missing.size()));
		return missing;
	}

	bool WorldState::hasEnoughPlayers() const {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		if (_player.level >= 8) return false;
		auto req = getLevelRequirement(_player.level);
		int playersOnTile = _vision.empty() ? 0 : _vision[0].playerCount;
		return playersOnTile >= req.playersNeeded;
	}

	void WorldState::clear() {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		_connected = false;
		_mapSize.reset();
		_player = PlayerState{};
		_player.inventory["nourriture"] = 10;
		_vision.clear();
		_visionHistory.clear();
		_lastVisionTime = 0;
		_lastInventoryTime = 0;
		_forkCount = 0;
		_levelUpCount = 0;
	}
} // namespace zappy