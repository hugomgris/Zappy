#pragma once

#include "../protocol/Message.hpp"

struct PlayerState {
	int			x = 0;
	int			y = 0;
	Orientation	orientation = Orientation::N;
	int			level = 1;
	Inventory	inventory;
	int			remainingSlots = 0;

	int food() const { return inventory.nourriture; }
};

struct WorldState {
	PlayerState				player;
	std::vector<VisionTile>	vision;
	int						mapWidth = 0;
	int						mapHeight = 0;

	// helper queries
	bool visionHasItem(const std::string& item) const {
		for (auto& tile : vision) {
			for (auto& tileItem : tile.items) {
				if (tileItem == item)
				 return true;
			}
		}
		return false;
	}

	std::optional<VisionTile> nearestTileWithItem(const std::string& item) const {
		for (auto& tile : vision) {
			for (auto& tileItem : tile.items) {
				if (tileItem == item)
					return tile;
			}
		}
		return std::nullopt;
	}

	int playersOnCurrentTile() const {
		if (!vision.empty()) {
			return vision[0].playerCount;
		}
		return 0;
	}

	int countItemOnCurrentTile(const std::string& item) const {
		if (!vision.empty()) {
			return vision[0].countItem(item);
		}
		return 0;
	}
};