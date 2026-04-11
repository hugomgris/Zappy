#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>

namespace zappy {
    // distance 0 means current tile, local positions relative to player facing north
    struct VisionTile {
        int							distance = 0;
        int							localX = 0;
        int							localY = 0;
        std::vector<std::string>	items;
        int							playerCount = 0;

        bool hasItem(const std::string& item) const;
        int countItem(const std::string& item) const;
    };

	struct MapSize {
		int x = 0;
		int y = 0;
	};

	struct PlayerState {
		int							id = 0;
		int							x = 0;
		int							y = 0;
		int							orientation = 1; // CLOCKWISE 1=N, 2=E, 3=S, 4=W
		int							level = 1;
		std::map<std::string, int>	inventory;
		int							remainingSlots = 0;
		int64_t						lastFoodEaten = 0;

		int getFood() const;
		bool hasItem(const std::string& item, int count = 1) const;
	};

	enum class ServerMessageType {
		Bienvenue,
		Welcome,
		Response,
		Error,
		Event,
		Message,
		Unknown
	};

	struct ServerMessage {
		ServerMessageType	type = ServerMessageType::Unknown;
		std::string			cmd;
		std::string			arg;
		std::string			status;
		std::string			raw;

		// parsed welcome data
		std::optional<MapSize>	mapSize;
		std::optional<int>		remainingClients;

		// parsed response data
		std::optional<std::vector<VisionTile>>		vision;
		std::optional<std::map<std::string, int>>	inventory;

		// event data
		std::optional<std::string> eventType;

		// nmessage data (broadcast)
		std::optional<int>			direction;
		std::optional<std::string>	messageText;

		bool isOk() const { return status == "ok" || arg == "ok"; }
		bool isKo() const { return status == "ko" || arg == "ko"; }
		bool isLevelUp() const {
			return type == ServerMessageType::Event
				&& (status == "Level up!" || arg == "Level up!" || (eventType.has_value() && *eventType == "Level up!"));
		}
		bool isDeath() const {
			return (type == ServerMessageType::Event && (status == "die" || arg == "die" || (eventType.has_value() && *eventType == "die"))) ||
			       (type == ServerMessageType::Response && cmd == "-" && arg == "die");
		}
	};

	ServerMessage parseServerMessage(const std::string& json);

	// navigation helpers
	int orientationFromString(const std::string& dir);
	std::string orientationToString(int orientation);
	void applyTurn(int& orientation, bool turnRight);
	void applyMove(int& x, int& y, int orientation, int mapWidth, int mapHeight);
} // namespace zappy