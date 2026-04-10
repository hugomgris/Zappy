#include "ProtocolTypes.hpp"
#include "helpers/Logger.hpp"

#include <cJSON.h>
#include <algorithm>
#include <cmath>

namespace zappy {
	bool VisionTile::hasItem(const std::string& item) const {
		return std::find(items.begin(), items.end(), item) != items.end();
	}

	int VisionTile::countItem(const std::string& item) const {
		return std::count(items.begin(), items.end(), item);
	}

	int PlayerState::getFood() const {
		auto it = inventory.find("nourriture");
		return it != inventory.end() ? it->second : 0;
	}

	bool PlayerState::hasItem(const std::string& item, int count) const {
		auto it = inventory.find(item);
		return it != inventory.end() && it->second >= count;
	}

	ServerMessage parseServerMessage(const std::string& json) {
		ServerMessage msg;
		msg.raw = json;

		cJSON* root = cJSON_Parse(json.c_str());
		if (!root) {
			Logger::error("Failed to parse JSON: " + json);
			return msg;
		}

		// parse type field
		cJSON* typeField = cJSON_GetObjectItem(root, "type");
		if (typeField && cJSON_IsString(typeField)) {
			std::string typeStr = typeField->valuestring;
			if (typeStr == "bienvenue") msg.type = ServerMessageType::Bienvenue;
			else if (typeStr == "welcome") msg.type = ServerMessageType::Welcome;
			else if (typeStr == "response") msg.type = ServerMessageType::Response;
			else if (typeStr == "error") msg.type = ServerMessageType::Error;
			else if (typeStr == "event") msg.type = ServerMessageType::Event;
			else if (typeStr == "message") msg.type = ServerMessageType::Message;
		}

		// parse cmd field
		cJSON* cmdField = cJSON_GetObjectItem(root, "cmd");
		if (cmdField && cJSON_IsString(cmdField)) {
			msg.cmd = cmdField->valuestring;
		}

		// parse arg field
		cJSON* argField = cJSON_GetObjectItem(root, "arg");
		if (argField && cJSON_IsString(argField)) {
			msg.arg = argField->valuestring;
		}
		
		// parse status field
		cJSON* statusField = cJSON_GetObjectItem(root, "status");
		if (statusField && cJSON_IsString(statusField)) {
			msg.status = statusField->valuestring;
		}

		// parse welcome msg
		if (msg.type == ServerMessageType::Welcome) {
			cJSON* remainingField = cJSON_GetObjectItem(root, "remaining_clients");
			if (remainingField && cJSON_IsNumber(remainingField)) {
				msg.remainingClients = remainingField->valueint;
			}

			cJSON* mapSizeField = cJSON_GetObjectItem(root, "map_size");
			if (mapSizeField) {
				cJSON* xField = cJSON_GetObjectItem(mapSizeField, "x");
				cJSON* yField = cJSON_GetObjectItem(mapSizeField, "y");

				if (xField && yField && cJSON_IsNumber(xField) && cJSON_IsNumber(yField)) {
					msg.mapSize = MapSize{xField->valueint, yField->valueint};
				}
			}
			
			// FIXED: Parse player_id if present
			cJSON* playerIdField = cJSON_GetObjectItem(root, "player_id");
			if (playerIdField && cJSON_IsNumber(playerIdField)) {
				// Store player ID if needed
			}
		}

		// parse vision response - FIXED coordinate calculation
		// Replace the vision parsing section (around line 180)
		if (msg.type == ServerMessageType::Response && msg.cmd == "voir") {
			cJSON* visionField = cJSON_GetObjectItem(root, "vision");
			if (visionField && cJSON_IsArray(visionField)) {
				std::vector<VisionTile> tiles;
				int arraySize = cJSON_GetArraySize(visionField);
				
				// Server vision order: 
				// Level 0: [current tile]
				// Level 1: [tile in front, left, right] - actually 3 tiles
				// Level 2: [row of 5 tiles]
				// etc.
				
				int currentLevel = 0;
				int tilesProcessed = 0;
				int tilesInCurrentLevel = 1; // Level 0 has 1 tile
				
				for (int i = 0; i < arraySize; i++) {
					if (tilesProcessed >= tilesInCurrentLevel) {
						currentLevel++;
						tilesInCurrentLevel = 2 * currentLevel + 1;
						tilesProcessed = 0;
					}
					
					cJSON* tileArray = cJSON_GetArrayItem(visionField, i);
					if (tileArray && cJSON_IsArray(tileArray)) {
						VisionTile tile;
						tile.distance = currentLevel;
						
						// Calculate local position
						int posInRow = tilesProcessed - currentLevel;
						tile.localX = posInRow;
						tile.localY = currentLevel;
						
						int tileSize = cJSON_GetArraySize(tileArray);
						for (int j = 0; j < tileSize; j++) {
							cJSON* item = cJSON_GetArrayItem(tileArray, j);
							if (item && cJSON_IsString(item)) {
								std::string itemStr = item->valuestring;
								if (itemStr == "player") {
									tile.playerCount++;
								} else {
									tile.items.push_back(itemStr);
								}
							}
						}
						tiles.push_back(tile);
					}
					tilesProcessed++;
				}
				msg.vision = tiles;
				
				// DEBUG: Log what we parsed
				Logger::debug("Parsed " + std::to_string(tiles.size()) + " vision tiles");
				for (const auto& tile : tiles) {
					Logger::debug("Tile dist=" + std::to_string(tile.distance) + 
								" items=" + std::to_string(tile.items.size()));
				}
			}
		}
		
		// parse inventory response
		if (msg.type == ServerMessageType::Response && msg.cmd == "inventaire") {
			cJSON* invField = cJSON_GetObjectItem(root, "inventaire");
			if (invField) {
				std::map<std::string, int> inv;
				cJSON* item = invField->child;
				while (item) {
					if (cJSON_IsNumber(item)) {
						inv[item->string] = item->valueint;
					}
					item = item->next;
				}
				msg.inventory = inv;
			}
		}
		
		// parse event
		if (msg.type == ServerMessageType::Event) {
			cJSON* eventField = cJSON_GetObjectItem(root, "event");
			if (eventField && cJSON_IsString(eventField)) {
				msg.eventType = eventField->valuestring;
				if (msg.arg.empty()) {
					msg.arg = eventField->valuestring;
				}
			}
		}
		
		// FIXED: Parse deplacement event (expulse response)
		if (msg.type == ServerMessageType::Response && msg.cmd == "deplacement") {
			if (!msg.status.empty()) {
				try {
					msg.direction = std::stoi(msg.status);
				} catch (...) {
					Logger::warn("Failed to parse deplacement direction: " + msg.status);
				}
			}
		}
		
		// parse broadcast message
		if (msg.type == ServerMessageType::Message) {
			// Server sends: {"type":"message","arg":"text","status":"K"}
			if (!msg.arg.empty()) {
				msg.messageText = msg.arg;
			}
			if (!msg.status.empty()) {
				try {
					msg.direction = std::stoi(msg.status);
				} catch (...) {
					Logger::warn("Failed to parse message direction: " + msg.status);
				}
			}
		}
		
		cJSON_Delete(root);
		return msg;
	}

	int orientationFromString(const std::string& dir) {
		if (dir == "N" || dir == "north") return 1;
		if (dir == "E" || dir == "east") return 2;
		if (dir == "S" || dir == "south") return 3;
		if (dir == "W" || dir == "west") return 4;
		return 1;
	}

	std::string orientationToString(int orientation) {
		switch (orientation) {
			case 1: return "N";
			case 2: return "E";
			case 3: return "S";
			case 4: return "W";
			default: return "N";
		}
	}

	void applyTurn(int& orientation, bool turnRight) {
		if (turnRight) {
			orientation = (orientation == 4) ? 1 : orientation + 1;
		} else {
			orientation = (orientation == 1) ? 4 : orientation - 1;
		}
	}

	void applyMove(int& x, int& y, int orientation, int mapWidth, int mapHeight) {
		switch (orientation) {
			case 1: y = (y - 1 + mapHeight) % mapHeight; break; // North: y decreases
			case 2: x = (x + 1) % mapWidth; break;              // East: x increases
			case 3: y = (y + 1) % mapHeight; break;             // South: y increases
			case 4: x = (x - 1 + mapWidth) % mapWidth; break;   // West: x decreases
		}
	}
} // namespace zappy