#pragma once

#include <string>
#include <vector>
#include <cJSON.h>
#include "app/ProtocolTypes.hpp"

namespace zappy {
namespace testing {

inline ServerMessage createMockBienvenue() {
	ServerMessage msg;
	msg.type = ServerMessageType::Bienvenue;
	msg.raw = R"({"type":"bienvenue","msg":"Welcome!"})";
	return msg;
}

inline ServerMessage createMockWelcome(int remaining, int mapX, int mapY) {
	ServerMessage msg;
	msg.type = ServerMessageType::Welcome;
	msg.remainingClients = remaining;
	msg.mapSize = MapSize{mapX, mapY};
	return msg;
}

inline ServerMessage createMockVisionResponse(const std::vector<std::vector<std::string>>& tiles) {
	ServerMessage msg;
	msg.type = ServerMessageType::Response;
	msg.cmd = "voir";
	
	std::vector<VisionTile> vision;
	for (size_t i = 0; i < tiles.size(); i++) {
		VisionTile tile;
		tile.distance = static_cast<int>(i);
		
		// Calculate coordinates
		int row = 0;
		int tilesInPrevRows = 0;
		while (tilesInPrevRows + (2 * row + 1) <= static_cast<int>(i)) {
			tilesInPrevRows += 2 * row + 1;
			row++;
		}
		tile.localX = static_cast<int>(i - tilesInPrevRows) - row;
		tile.localY = row;
		
		for (const auto& item : tiles[i]) {
			if (item == "player") {
				tile.playerCount++;
			} else {
				tile.items.push_back(item);
			}
		}
		vision.push_back(tile);
	}
	
	msg.vision = vision;
	return msg;
}

inline ServerMessage createMockInventoryResponse(const std::map<std::string, int>& inv) {
	ServerMessage msg;
	msg.type = ServerMessageType::Response;
	msg.cmd = "inventaire";
	msg.inventory = inv;
	return msg;
}

inline ServerMessage createMockOkResponse(const std::string& cmd) {
	ServerMessage msg;
	msg.type = ServerMessageType::Response;
	msg.cmd = cmd;
	msg.status = "ok";
	return msg;
}

inline ServerMessage createMockKoResponse(const std::string& cmd) {
	ServerMessage msg;
	msg.type = ServerMessageType::Response;
	msg.cmd = cmd;
	msg.status = "ko";
	return msg;
}

inline ServerMessage createMockLevelUpEvent() {
	ServerMessage msg;
	msg.type = ServerMessageType::Event;
	msg.arg = "Level up!";
	return msg;
}

inline ServerMessage createMockDeathEvent() {
	ServerMessage msg;
	msg.type = ServerMessageType::Event;
	msg.arg = "die";
	return msg;
}

inline ServerMessage createMockBroadcast(const std::string& text, int direction) {
	ServerMessage msg;
	msg.type = ServerMessageType::Message;
	msg.messageText = text;
	msg.direction = direction;
	return msg;
}

// Helper to wait for condition with timeout
template<typename Func>
bool waitFor(Func condition, int timeoutMs = 5000, int intervalMs = 100) {
	int elapsed = 0;
	while (elapsed < timeoutMs) {
		if (condition()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
		elapsed += intervalMs;
	}
	return false;
}

} // namespace testing
} // namespace zappy