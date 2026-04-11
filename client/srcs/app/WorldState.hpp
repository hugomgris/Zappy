#pragma once

#include "ProtocolTypes.hpp"

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>
#include <mutex>
#include <deque>

namespace zappy {
	// level requirements for incantation
	struct LevelRequirement {
		int							playersNeeded;
		std::map<std::string, int>	stonesNeeded;
	};

	class WorldState {
		private:
			bool					_connected = false;
			std::optional<MapSize>	_mapSize;
			PlayerState				_player;
			std::vector<VisionTile> _vision;
			std::deque<std::vector<VisionTile>> _visionHistory;  // FIXED: Added history

			int64_t	_lastVisionTime = 0;
			int64_t	_lastInventoryTime = 0;
			int64_t	_lastFoodTime = 0;

			int	_forkCount = 0;
			int	_levelUpCount = 0;

		mutable std::recursive_mutex	_mutex;

			void updateInventory(const std::map<std::string, int>& inv);
			void updateVision(const std::vector<VisionTile>& vision);

		public:
			WorldState();

			// update from server msgs
			void onWelcome(const ServerMessage& msg);
			void onResponse(const ServerMessage& msg);
			void onEvent(const ServerMessage& msg);
			void onMessage(const ServerMessage& msg);

			// state queries
			bool isConnected() const { return _connected; }
			bool hasMapSize() const { return _mapSize.has_value(); }
			MapSize getMapSize() const { return _mapSize.value_or(MapSize{0, 0}); }

			const PlayerState& getPlayer() const { return _player; }
			const std::vector<VisionTile>& getVision() const { return _vision; }
			const std::map<std::string, int>& getInventory() const { return _player.inventory; }

			int getFood() const { return _player.getFood(); }
			int getLevel() const { return _player.level; }
			int getPlayersOnTile() const;

			// vision queries
			bool seesItem(const std::string& item) const;
			std::optional<VisionTile> getNearestItem(const std::string& item) const;
			std::vector<VisionTile> getTilesWithItem(const std::string& item) const;

			// incantation
			bool canIncantate() const;
			bool hasStonesForIncantation() const;
			LevelRequirement getLevelRequirement(int level) const;
			std::vector<std::string> getMissingStones() const;
			bool hasEnoughPlayers() const;

			// stats
			int64_t getLastVisionTime() const { return _lastVisionTime; }
			int64_t getLastInventoryTime() const { return _lastInventoryTime; }
			int getForkCount() const { return _forkCount; }
			int getLevelUpCount() const { return _levelUpCount; }

			// reset!
			void clear();
	};
} //namespace zappy