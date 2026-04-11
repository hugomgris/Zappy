#include "ProtocolTypes.hpp"
#include "../helpers/Logger.hpp"

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

    cJSON* typeField = cJSON_GetObjectItem(root, "type");
    if (typeField && cJSON_IsString(typeField)) {
        std::string typeStr = typeField->valuestring;
        if (typeStr == "bienvenue")  msg.type = ServerMessageType::Bienvenue;
        else if (typeStr == "welcome")  msg.type = ServerMessageType::Welcome;
        else if (typeStr == "response") msg.type = ServerMessageType::Response;
        else if (typeStr == "error")    msg.type = ServerMessageType::Error;
        else if (typeStr == "event")    msg.type = ServerMessageType::Event;
        else if (typeStr == "message")  msg.type = ServerMessageType::Message;
    }

    cJSON* cmdField = cJSON_GetObjectItem(root, "cmd");
    if (cmdField && cJSON_IsString(cmdField))
        msg.cmd = cmdField->valuestring;

    cJSON* argField = cJSON_GetObjectItem(root, "arg");
    if (argField && cJSON_IsString(argField))
        msg.arg = argField->valuestring;

    cJSON* statusField = cJSON_GetObjectItem(root, "status");
    if (statusField && cJSON_IsString(statusField))
        msg.status = statusField->valuestring;

    // Welcome
    if (msg.type == ServerMessageType::Welcome) {
        cJSON* remainingField = cJSON_GetObjectItem(root, "remaining_clients");
        if (remainingField && cJSON_IsNumber(remainingField))
            msg.remainingClients = remainingField->valueint;

        cJSON* orientationField = cJSON_GetObjectItem(root, "orientation");
        if (orientationField && cJSON_IsNumber(orientationField))
            msg.playerOrientation = orientationField->valueint;

        cJSON* mapSizeField = cJSON_GetObjectItem(root, "map_size");
        if (mapSizeField) {
            cJSON* xField = cJSON_GetObjectItem(mapSizeField, "x");
            cJSON* yField = cJSON_GetObjectItem(mapSizeField, "y");
            if (xField && yField && cJSON_IsNumber(xField) && cJSON_IsNumber(yField))
                msg.mapSize = MapSize{xField->valueint, yField->valueint};
        }
    }

    // Vision (voir response)
    if (msg.type == ServerMessageType::Response && msg.cmd == "voir") {
        if (msg.status == "ko") {
            Logger::warn("Failed to execute voir command");
            cJSON_Delete(root);
            return msg;
        }

        cJSON* visionField = cJSON_GetObjectItem(root, "vision");
        if (visionField && cJSON_IsArray(visionField)) {
            std::vector<VisionTile> tiles;
            int arraySize = cJSON_GetArraySize(visionField);

            // Server sends tiles in row order:
            //   d=0: 1 tile  (the player's own tile)
            //   d=1: 3 tiles (left, center, right at distance 1)
            //   d=2: 5 tiles
            //   etc.
            // Within each row, tiles go left-to-right relative to the player's facing.
            // So for row d, tile i has localX = i − d, localY = d.
            int currentLevel    = 0;
            int tilesInLevel    = 1;
            int tilesProcessed  = 0;

            for (int i = 0; i < arraySize; i++) {
                if (tilesProcessed >= tilesInLevel) {
                    currentLevel++;
                    tilesInLevel   = 2 * currentLevel + 1;
                    tilesProcessed = 0;
                }

                cJSON* tileArray = cJSON_GetArrayItem(visionField, i);
                if (tileArray && cJSON_IsArray(tileArray)) {
                    VisionTile tile;
                    tile.distance = currentLevel;
                    tile.localX   = tilesProcessed - currentLevel; // negative=left, 0=center, positive=right
                    tile.localY   = currentLevel;                  // distance forward

                    int tileSize = cJSON_GetArraySize(tileArray);
                    for (int j = 0; j < tileSize; j++) {
                        cJSON* item = cJSON_GetArrayItem(tileArray, j);
                        if (item && cJSON_IsString(item)) {
                            std::string itemStr = item->valuestring;
                            if (itemStr == "player")
                                tile.playerCount++;
                            else
                                tile.items.push_back(itemStr);
                        }
                    }
                    tiles.push_back(tile);
                }
                tilesProcessed++;
            }
            msg.vision = tiles;

            Logger::debug("Parsed " + std::to_string(tiles.size()) + " vision tiles");
        }
    }

    // Inventory
    if (msg.type == ServerMessageType::Response && msg.cmd == "inventaire") {
        cJSON* invField = cJSON_GetObjectItem(root, "inventaire");
        if (invField) {
            std::map<std::string, int> inv;
            cJSON* item = invField->child;
            while (item) {
                if (cJSON_IsNumber(item))
                    inv[item->string] = item->valueint;
                item = item->next;
            }
            msg.inventory = inv;
        }
    }

    // Event
    if (msg.type == ServerMessageType::Event) {
        cJSON* eventField = cJSON_GetObjectItem(root, "event");
        if (eventField && cJSON_IsString(eventField)) {
            msg.eventType = eventField->valuestring;
            if (msg.arg.empty())
                msg.arg = eventField->valuestring;
        }
    }

    // Expulse / deplacement
    if (msg.type == ServerMessageType::Response && msg.cmd == "deplacement") {
        if (!msg.status.empty()) {
            try { msg.direction = std::stoi(msg.status); }
            catch (...) { Logger::warn("Failed to parse deplacement direction: " + msg.status); }
        }
    }

    // Broadcast message
    if (msg.type == ServerMessageType::Message) {
        // Server sends: {"type":"message","arg":"<text>","status":"<K>"}
        // where status is the direction integer as a string (0-8).
        if (!msg.arg.empty())
            msg.messageText = msg.arg;

        if (!msg.status.empty()) {
            try { msg.direction = std::stoi(msg.status); }
            catch (...) { Logger::warn("Failed to parse message direction: " + msg.status); }
        }
    }

    cJSON_Delete(root);
    return msg;
}

/*
 * orientationFromString — convert server direction string to 1-indexed int.
 *
 * FIX 7: The server's internal direction enum is 0-indexed (NORTH=0, EAST=1,
 * SOUTH=2, WEST=3). But the server sends orientation in responses/welcome as
 * a 0-based integer too (see m_serialize_player → "orientation": p->dir).
 * We store orientation as 1-indexed (1=N,2=E,3=S,4=W) in PlayerState to match
 * the convention used by applyTurn / applyMove below.
 * This function converts the raw server int → our 1-indexed convention.
 */
int orientationFromString(const std::string& dir) {
    if (dir == "N" || dir == "north" || dir == "0") return 1;
    if (dir == "E" || dir == "east"  || dir == "1") return 2;
    if (dir == "S" || dir == "south" || dir == "2") return 3;
    if (dir == "W" || dir == "west"  || dir == "3") return 4;
    return 1;
}

/*
 * orientationFromServer — convert raw 0-based server int to our 1-indexed convention.
 * Use this when reading the "orientation" field from a JSON response.
 * FIX 7: previously the client stored the raw 0-3 value, making all direction
 * arithmetic off by one relative to the turn/move helpers below.
 */
int orientationFromServer(int serverOrientation) {
    // server: 0=N,1=E,2=S,3=W  →  client: 1=N,2=E,3=S,4=W
    return (serverOrientation % 4) + 1;
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

/*
 * applyTurn — update 1-indexed orientation after a turn command.
 * Clockwise (droite/right): N→E→S→W→N  (1→2→3→4→1)
 * Counter-clockwise (gauche/left): N→W→S→E→N  (1→4→3→2→1)
 */
void applyTurn(int& orientation, bool turnRight) {
    if (turnRight)
        orientation = (orientation % 4) + 1;     // 1→2→3→4→1
    else
        orientation = ((orientation - 2 + 4) % 4) + 1; // 1→4→3→2→1
}

/*
 * applyMove — update world position after avance.
 * Server Y increases going South, Y decreases going North.
 * FIX 7: Uses our 1-indexed orientation convention.
 */
void applyMove(int& x, int& y, int orientation, int mapWidth, int mapHeight) {
    switch (orientation) {
        case 1: y = (y - 1 + mapHeight) % mapHeight; break; // North: y decreases
        case 2: x = (x + 1) % mapWidth;              break; // East:  x increases
        case 3: y = (y + 1) % mapHeight;              break; // South: y increases
        case 4: x = (x - 1 + mapWidth) % mapWidth;   break; // West:  x decreases
    }
}

} // namespace zappy
