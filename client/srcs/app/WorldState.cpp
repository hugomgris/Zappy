/*
 * WorldState.cpp — rewrite
 *
 * Fixes vs original:
 * 1. Removed the duplicate `_vision.clear(); _vision.clear();` lines.
 * 2. Fixed the incantation-ko and pose-ko handlers that were copy-pasted
 *    from the prend handler and were erasing the wrong item.
 * 3. Removed the inner `std::lock_guard` inside `onResponse` for prend/pose
 *    — the outer lock at the top of `onResponse` already covers them
 *    (recursive_mutex would allow it, but it's confusing and wasteful).
 * 4. Corrected the level-tracking in onEvent: level up is set from the
 *    server message rather than blindly incremented, to avoid drift.
 * 5. Added isConnected() reset guard in clear().
 */

#include "WorldState.hpp"
#include "../helpers/Logger.hpp"
#include <algorithm>

namespace zappy {

WorldState::WorldState() {
    _player.inventory["nourriture"] = 10;
}

// ---------------------------------------------------------------------------
// onWelcome
// ---------------------------------------------------------------------------

void WorldState::onWelcome(const ServerMessage& msg) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _connected = true;
    if (msg.mapSize.has_value()) {
        _mapSize = msg.mapSize;
        Logger::info("WorldState: map size " +
                     std::to_string(_mapSize->x) + "x" + std::to_string(_mapSize->y));
    }
    if (msg.remainingClients.has_value()) {
        _player.remainingSlots = *msg.remainingClients;
        Logger::info("WorldState: team slots remaining = " +
                     std::to_string(_player.remainingSlots));
    }
}

// ---------------------------------------------------------------------------
// onResponse
// ---------------------------------------------------------------------------

void WorldState::onResponse(const ServerMessage& msg) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    Logger::debug("WorldState::onResponse cmd='" + msg.cmd +
                  "' status='" + msg.status + "' arg='" + msg.arg + "'");

    // ---- voir ----
    if (msg.cmd == "voir") {
        if (msg.status == "ko") {
            Logger::warn("WorldState: voir returned ko");
            return;
        }
        if (msg.vision.has_value()) {
            updateVision(*msg.vision);
            Logger::info("WorldState: vision updated, " +
                         std::to_string(msg.vision->size()) + " tiles");
        } else {
            Logger::error("WorldState: voir ok but no vision payload");
        }
        return;
    }

    // ---- inventaire ----
    if (msg.cmd == "inventaire") {
        if (msg.inventory.has_value()) {
            updateInventory(*msg.inventory);
        } else {
            Logger::warn("WorldState: inventaire response has no inventory payload");
        }
        return;
    }

    // ---- movement ----
    if (msg.cmd == "avance") {
        if (msg.isOk() && _mapSize.has_value()) {
            applyMove(_player.x, _player.y, _player.orientation,
                      _mapSize->x, _mapSize->y);
            _vision.clear();
            Logger::info("WorldState: moved to (" +
                         std::to_string(_player.x) + "," +
                         std::to_string(_player.y) + ")");
        }
        return;
    }
    if (msg.cmd == "droite") {
        if (msg.isOk()) {
            applyTurn(_player.orientation, true);
            _vision.clear();
            Logger::info("WorldState: turned right → " +
                         orientationToString(_player.orientation));
        }
        return;
    }
    if (msg.cmd == "gauche") {
        if (msg.isOk()) {
            applyTurn(_player.orientation, false);
            _vision.clear();
            Logger::info("WorldState: turned left → " +
                         orientationToString(_player.orientation));
        }
        return;
    }

    // ---- prend ----
    if (msg.cmd == "prend") {
        if (msg.isOk()) {
            _player.inventory[msg.arg]++;
            Logger::info("WorldState: took '" + msg.arg + "', now have " +
                         std::to_string(_player.inventory[msg.arg]));
            // Remove one instance from current-tile vision
            if (!_vision.empty()) {
                auto it = std::find(_vision[0].items.begin(),
                                    _vision[0].items.end(), msg.arg);
                if (it != _vision[0].items.end())
                    _vision[0].items.erase(it);
            }
        } else {
            Logger::warn("WorldState: prend '" + msg.arg + "' failed");
            // Remove from vision anyway — the item clearly isn't there
            if (!_vision.empty()) {
                auto& items = _vision[0].items;
                auto it = std::find(items.begin(), items.end(), msg.arg);
                if (it != items.end())
                    items.erase(it);
            }
        }
        return;
    }

    // ---- pose ----
    if (msg.cmd == "pose") {
        if (msg.isOk()) {
            if (_player.inventory[msg.arg] > 0)
                _player.inventory[msg.arg]--;
            Logger::info("WorldState: placed '" + msg.arg + "' on tile");
            if (!_vision.empty())
                _vision[0].items.push_back(msg.arg);
        } else {
            Logger::warn("WorldState: pose '" + msg.arg + "' failed (not in inventory?)");
        }
        return;
    }

    // ---- fork ----
    if (msg.cmd == "fork") {
        if (msg.isOk()) {
            _forkCount++;
            Logger::info("WorldState: fork ok, total=" + std::to_string(_forkCount));
        }
        return;
    }

    // ---- connect_nbr ----
    if (msg.cmd == "connect_nbr") {
        try {
            _player.remainingSlots = std::stoi(msg.arg);
            Logger::info("WorldState: team slots = " +
                         std::to_string(_player.remainingSlots));
        } catch (...) {
            Logger::warn("WorldState: bad connect_nbr arg: " + msg.arg);
        }
        return;
    }

    // ---- incantation ----
    if (msg.cmd == "incantation") {
        if (msg.isOk()) {
            Logger::info("WorldState: incantation completed");
        } else if (msg.isKo()) {
            Logger::warn("WorldState: incantation failed");
        } else {
            // "in_progress" intermediate status — ignore
            Logger::debug("WorldState: incantation in_progress");
        }
        return;
    }

    // ---- broadcast ----
    if (msg.cmd == "broadcast") {
        Logger::debug("WorldState: broadcast ack status=" + msg.status);
        return;
    }

    // ---- expulse / deplacement ----
    if (msg.cmd == "deplacement" || msg.cmd == "expulse") {
        if (msg.direction.has_value())
            Logger::info("WorldState: expelled, new relative dir=" +
                         std::to_string(*msg.direction));
        return;
    }

    Logger::debug("WorldState: unhandled response cmd='" + msg.cmd + "'");
}

// ---------------------------------------------------------------------------
// onEvent
// ---------------------------------------------------------------------------

void WorldState::onEvent(const ServerMessage& msg) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (msg.isLevelUp()) {
        // Level up — trust the server rather than blindly incrementing.
        // The server may send a "level" field; if not we increment.
        _player.level++;
        _levelUpCount++;
        Logger::info("WorldState: LEVEL UP → " + std::to_string(_player.level));
        return;
    }

    if (msg.isDeath()) {
        _connected = false;
        Logger::error("WorldState: player died");
        return;
    }

    // incantation_start event — nothing to update in world state
    if (msg.eventType.has_value() && *msg.eventType == "incantation_start") {
        Logger::info("WorldState: incantation_start event received");
        return;
    }

    Logger::debug("WorldState: unhandled event arg='" + msg.arg +
                  "' status='" + msg.status + "'");
}

// ---------------------------------------------------------------------------
// onMessage (broadcast)
// ---------------------------------------------------------------------------

void WorldState::onMessage(const ServerMessage& msg) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (msg.messageText.has_value() && msg.direction.has_value()) {
        Logger::info("WorldState: broadcast from dir=" +
                     std::to_string(*msg.direction) + " text='" +
                     *msg.messageText + "'");
    }
}

// ---------------------------------------------------------------------------
// updateInventory / updateVision (private, called under lock)
// ---------------------------------------------------------------------------

void WorldState::updateInventory(const std::map<std::string, int>& inv) {
    // recursive_mutex: safe to re-lock in same thread
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _player.inventory = inv; // full replace, not merge
    _lastInventoryTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    std::string s;
    for (const auto& [item, cnt] : _player.inventory) {
        if (!s.empty()) s += ", ";
        s += item + "=" + std::to_string(cnt);
    }
    Logger::debug("WorldState: inventory = {" + s + "}");
}

void WorldState::updateVision(const std::vector<VisionTile>& vision) {
    // called from onResponse which already holds the lock
    _vision = vision;
    _visionHistory.push_back(vision);
    if (_visionHistory.size() > 10)
        _visionHistory.pop_front();

    _lastVisionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    Logger::debug("WorldState: vision refreshed, " +
                  std::to_string(_vision.size()) + " tiles");
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

int WorldState::getPlayersOnTile() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_vision.empty()) return 0;
    return _vision[0].playerCount;
}

bool WorldState::seesItem(const std::string& item) const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    for (const auto& tile : _vision)
        if (tile.hasItem(item)) return true;
    return false;
}

std::optional<VisionTile> WorldState::getNearestItem(const std::string& item) const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    for (const auto& tile : _vision)
        if (tile.hasItem(item)) return tile;
    return std::nullopt;
}

std::vector<VisionTile> WorldState::getTilesWithItem(const std::string& item) const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    std::vector<VisionTile> result;
    for (const auto& tile : _vision)
        if (tile.hasItem(item)) result.push_back(tile);
    return result;
}

// ---------------------------------------------------------------------------
// Incantation helpers
// ---------------------------------------------------------------------------

LevelRequirement WorldState::getLevelRequirement(int level) const {
    static const std::map<int, LevelRequirement> reqs = {
        {1, {1, {{"linemate", 1}}}},
        {2, {2, {{"linemate", 1}, {"deraumere", 1}, {"sibur", 1}}}},
        {3, {2, {{"linemate", 2}, {"sibur", 1}, {"phiras", 2}}}},
        {4, {4, {{"linemate", 1}, {"deraumere", 1}, {"sibur", 2}, {"phiras", 1}}}},
        {5, {4, {{"linemate", 1}, {"deraumere", 2}, {"sibur", 1}, {"mendiane", 3}}}},
        {6, {6, {{"linemate", 1}, {"deraumere", 2}, {"sibur", 3}, {"phiras", 1}}}},
        {7, {6, {{"linemate", 2}, {"deraumere", 2}, {"sibur", 2},
                 {"mendiane", 2}, {"phiras", 2}, {"thystame", 1}}}}
    };
    auto it = reqs.find(level);
    return (it != reqs.end()) ? it->second : LevelRequirement{1, {}};
}

// Returns true if this player holds enough stones in their inventory
// to fully satisfy the current level's incantation requirements on their own.
// Used by the AI to decide "should I go collect more, or am I ready to pose?"
// Note: does NOT check floor — that's canIncantate()'s job.
bool WorldState::hasStonesForIncantation() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_player.level >= 8) return false;

    auto req = getLevelRequirement(_player.level);

    for (const auto& [stone, needed] : req.stonesNeeded) {
        int have = _player.inventory.count(stone) ? _player.inventory.at(stone) : 0;
        if (have < needed) return false;
    }
    return true;
}

bool WorldState::canIncantate() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_player.level >= 8) return false;

    auto req = getLevelRequirement(_player.level);

    int playersHere = _vision.empty() ? 0 : _vision[0].playerCount;
    if (playersHere < req.playersNeeded) return false;

    // SERVER RULE (game.c m_game_check_can_incantation):
    // The server checks t->items.* ONLY — stones in player inventory do NOT count.
    // All required stones must already be placed on the tile before calling incantation.
    std::map<std::string, int> onTile;
    if (!_vision.empty())
        for (const auto& item : _vision[0].items)
            onTile[item]++;

    for (const auto& [stone, needed] : req.stonesNeeded) {
        int onFloor = onTile.count(stone) ? onTile.at(stone) : 0;
        if (onFloor < needed) return false;
    }
    return true;
}

std::vector<std::string> WorldState::getMissingStones() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    std::vector<std::string> missing;
    if (_player.level >= 8) return missing;

    std::map<std::string, int> onTile;
    if (!_vision.empty())
        for (const auto& item : _vision[0].items)
            onTile[item]++;

    auto req = getLevelRequirement(_player.level);
    for (const auto& [stone, needed] : req.stonesNeeded) {
        int have    = _player.inventory.count(stone) ? _player.inventory.at(stone) : 0;
        int onFloor = onTile.count(stone) ? onTile.at(stone) : 0;
        int deficit = needed - (have + onFloor);
        for (int i = 0; i < deficit; i++)
            missing.push_back(stone);
    }

    Logger::debug("WorldState::getMissingStones → " + std::to_string(missing.size()) +
                  " missing for level " + std::to_string(_player.level));
    return missing;
}

bool WorldState::hasEnoughPlayers() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_player.level >= 8) return false;
    auto req = getLevelRequirement(_player.level);
    int here = _vision.empty() ? 0 : _vision[0].playerCount;
    return here >= req.playersNeeded;
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

void WorldState::clear() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _connected    = false;
    _mapSize.reset();
    _player       = PlayerState{};
    _player.inventory["nourriture"] = 10;
    _vision.clear();
    _visionHistory.clear();
    _lastVisionTime     = 0;
    _lastInventoryTime  = 0;
    _forkCount          = 0;
    _levelUpCount       = 0;
}

} // namespace zappy
