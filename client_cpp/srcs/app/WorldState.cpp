#include "WorldState.hpp"
#include "helpers/Logger.hpp"

namespace zappy {
	WorldState::WorldState() {
		_player.inventory["nourriture"] = 10;
	}

	void WorldState::onWelcome(const ServerMessage& msg) {
		std::lock_guard<std::mutex> lock(_mutex);

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
		std::lock_guard<std::mutex> lock(_mutex);
		
		if (msg.cmd == "voir" && msg.vision.has_value()) {
			updateVision(*msg.vision);
		}
		else if (msg.cmd == "inventaire" && msg.inventory.has_value()) {
			updateInventory(*msg.inventory);
		}
		else if (msg.cmd == "avance" && msg.isOk()) {
			if (_mapSize.has_value()) {
				applyMove(_player.x, _player.y, _player.orientation, _mapSize->x, _mapSize->y);
			}
			Logger::debug("Moved to (" + std::to_string(_player.x) + "," + std::to_string(_player.y) + ")");
		}
		else if (msg.cmd == "droite" && msg.isOk()) {
			applyTurn(_player.orientation, true);
			Logger::debug("Turned right, now facing " + orientationToString(_player.orientation));
		}
		else if (msg.cmd == "gauche" && msg.isOk()) {
			applyTurn(_player.orientation, false);
			Logger::debug("Turned left, now facing " + orientationToString(_player.orientation));
		}
		else if (msg.cmd == "fork" && msg.isOk()) {
			_forkCount++;
			Logger::info("Fork successful! Total forks: " + std::to_string(_forkCount));
		}
		else if (msg.cmd == "connect_nbr") {
			try {
				_player.remainingSlots = std::stoi(msg.arg);
				Logger::info("Team slots available: " + std::to_string(_player.remainingSlots));
			} catch (...) {}
		}
		else if (msg.cmd == "incantation") {
			if (msg.arg == "in_progress") {
				Logger::info("Incantation in progress...");
			}
		}
	}

	void WorldState::onEvent(const ServerMessage& msg) {
		std::lock_guard<std::mutex> lock(_mutex);
		
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
		std::lock_guard<std::mutex> lock(_mutex);
		
		if (msg.messageText.has_value() && msg.direction.has_value()) {
			Logger::info("Broadcast from dir " + std::to_string(*msg.direction) + ": " + *msg.messageText);
		}
	}

	void WorldState::updateInventory(const std::map<std::string, int>& inv) {
		_player.inventory = inv;
		_lastInventoryTime = std::chrono::duration_cast<std::chrono::milliseconds> ( std::chrono::steady_clock::now().time_since_epoch()).count();

		std::string invStr;

		for (const auto& [item, count] : _player.inventory) {
			if (!invStr.empty()) invStr += ", ";
			invStr += item + "=" + std::to_string(count);
		}
		Logger::debug("Inventory updated: " + invStr);
	}

	void WorldState::updateVision(const std::vector<VisionTile>& vision) {
		_vision = vision;
		_lastVisionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count();
		
		if (!_vision.empty()) {
			_player.x = 0;  // Relative to vision, actual position tracked via moves
			_player.y = 0;
		}
		
		Logger::debug("Vision updated: " + std::to_string(_vision.size()) + " tiles");
	}
	
	int WorldState::getPlayersOnTile() const {
		std::lock_guard<std::mutex> lock(_mutex);
		if (_vision.empty()) return 0;
		return _vision[0].playerCount;
	}

	bool WorldState::seesItem(const std::string& item) const {
		std::lock_guard<std::mutex> lock(_mutex);
		for (const auto& tile : _vision) {
			if (tile.hasItem(item)) return true;
		}
		return false;
	}

	std::optional<VisionTile> WorldState::getNearestItem(const std::string& item) const {
		std::lock_guard<std::mutex> lock(_mutex);
		for (const auto& tile : _vision) {
			if (tile.hasItem(item)) return tile;
		}
		return std::nullopt;
	}

	std::vector<VisionTile> WorldState::getTilesWithItem(const std::string& item) const {
		std::lock_guard<std::mutex> lock(_mutex);
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

	bool WorldState::canIncantate() const {
		std::lock_guard<std::mutex> lock(_mutex);

		if (_player.level >= 8) return false;

		auto req = getLevelRequirement(_player.level);

		// check players on tile
		if (getPlayersOnTile() < req.playersNeeded) return false;

		// check stones in inv (assuming placed on tile)
		for (const auto& [stone, needed] : req.stonesNeeded) {
			auto it = _player.inventory.find(stone);
			if (it == _player.inventory.end() || it->second < needed) {
				return false;
			}
		}

		return true;
	}

	std::vector<std::string> WorldState::getMissingStones() const {
		std::lock_guard<std::mutex> lock(_mutex);
		std::vector<std::string> missing;
		
		if (_player.level >= 8) return missing;
		
		auto req = getLevelRequirement(_player.level);
		for (const auto& [stone, needed] : req.stonesNeeded) {
			auto it = _player.inventory.find(stone);
			int have = (it != _player.inventory.end()) ? it->second : 0;
			if (have < needed) {
				for (int i = 0; i < needed - have; i++) {
					missing.push_back(stone);
				}
			}
		}
		
		return missing;
	}

	bool WorldState::hasEnoughPlayers() const {
		std::lock_guard<std::mutex> lock(_mutex);
		if (_player.level >= 8) return false;
		auto req = getLevelRequirement(_player.level);
		return getPlayersOnTile() >= req.playersNeeded;
	}

	void WorldState::clear() {
		std::lock_guard<std::mutex> lock(_mutex);
		_connected = false;
		_mapSize.reset();
		_player = PlayerState{};
		_player.inventory["nourriture"] = 10;
		_vision.clear();
		_lastVisionTime = 0;
		_lastInventoryTime = 0;
		_forkCount = 0;
		_levelUpCount = 0;
	}
} // namespace zappy