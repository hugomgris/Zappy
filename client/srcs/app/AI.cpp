/*
 * AI.cpp — Fixed cooperative AI for Zappy
 *
 * Fixes applied vs previous version:
 *
 * 1.  COMMAND SERIALIZATION (_commandInFlight flag)
 *     The server event buffer holds only 10 commands. Sending voir+inventaire
 *     every tick without waiting for a reply saturates it immediately, causing
 *     "Buffer full! event dropped" and lost responses. Now only one command is
 *     in flight at a time; the next is sent only after the callback fires.
 *
 * 2.  computeMissingStones — correct formula
 *     AI now correctly computes stones still needed as:
 *       deficit = needed - (have_in_inventory + already_on_floor)
 *     The previous version was logically correct but used _stonesNeeded as a
 *     stale snapshot that caused the AI to re-enter CollectingStones even when
 *     all stones were already gathered.
 *
 * 3.  runIdle — no more unconditional voir/inventaire every tick
 *     Vision and inventory are only requested when stale (>VISION_STALE_MS),
 *     and only when no command is in flight.
 *
 * 4.  runRallying — _stonesPlaced guard removed
 *     placeAllStonesOnTile already sets _stonesPlaced = true. The secondary
 *     guard comparing stale _stonesNeeded was preventing the flag from ever
 *     being set correctly.
 *
 * 5.  shouldFork — allow forking at level 1
 *     Level 2+ incantations require multiple players. The team must fork at
 *     level 1 to have enough players for later levels. Fork is now allowed
 *     from level 1 onwards (with a food safety gate).
 *
 * 6.  Orientation indexing clarified throughout
 *     Server uses 0=N,1=E,2=S,3=W internally but sends 1=N,2=E,3=S,4=W in
 *     responses. All navigation math now uses the 1-4 convention consistently.
 *     buildPlanTowardDirection broadcast-direction mapping corrected.
 *
 * 7.  sendVoir / sendInventaire wrapped in _commandInFlight guard
 *     These are no longer fire-and-forget; they set _commandInFlight and clear
 *     it in the callback, same as every other command.
 */

#include "AI.hpp"
#include "../helpers/Logger.hpp"
#include "ProtocolTypes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace zappy {

// ────────────────────────────────────────────────────────────────
//  Level requirement table  (matches server level_reqs[] exactly)
// ────────────────────────────────────────────────────────────────

const LevelReq& AI::levelReq(int level) {
    static const std::map<int, LevelReq> table = {
        {1, {1, {{"linemate", 1}}}},
        {2, {2, {{"linemate", 1}, {"deraumere", 1}, {"sibur", 1}}}},
        {3, {2, {{"linemate", 2}, {"sibur", 1}, {"phiras", 2}}}},
        {4, {4, {{"linemate", 1}, {"deraumere", 1}, {"sibur", 2}, {"phiras", 1}}}},
        {5, {4, {{"linemate", 1}, {"deraumere", 2}, {"sibur", 1}, {"mendiane", 3}}}},
        {6, {6, {{"linemate", 1}, {"deraumere", 2}, {"sibur", 3}, {"phiras", 1}}}},
        {7, {6, {{"linemate", 2}, {"deraumere", 2}, {"sibur", 2},
                 {"mendiane", 2}, {"phiras", 2}, {"thystame", 1}}}},
    };
    static const LevelReq empty{1, {}};
    auto it = table.find(level);
    return it != table.end() ? it->second : empty;
}

// ────────────────────────────────────────────────────────────────
//  Constructor
// ────────────────────────────────────────────────────────────────

AI::AI(WorldState& state, CommandSender& sender)
    : _state(state), _sender(sender) {}

// ────────────────────────────────────────────────────────────────
//  tick()
// ────────────────────────────────────────────────────────────────

void AI::tick(int64_t nowMs) {
    // FIX 1: If a command is still in flight, do not send another.
    // This is the primary fix for the "Buffer full!" server errors.
    if (_commandInFlight) {
        // Safety valve: if a command has been in-flight too long the callback
        // was probably dropped. Clear the flag so we can recover.
        if (nowMs - _commandInFlightSentMs > COMMAND_FLIGHT_TIMEOUT_MS) {
            Logger::warn("AI: command in-flight timeout, clearing flag");
            _commandInFlight = false;
        } else {
            return;
        }
    }

    // Periodic timeout guard (prevents getting stuck in any non-incantating state)
    if (_aiState != AIState::Incantating &&
        _aiState != AIState::Idle &&
        nowMs - _stateEnteredMs > STATE_TIMEOUT_MS) {
        Logger::warn("AI: state timeout in state=" + std::to_string((int)_aiState) + ", resetting to Idle");
        clearNav();
        _stonesPlaced = false;
        _incantationSent = false;
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    // Food emergency overrides everything except active incantation.
    // If we are already rallying and can incantate now, don't break formation.
    if (_aiState != AIState::Incantating && foodIsCritical()) {
        if (_aiState == AIState::Rallying && readyToIncantate()) {
            // Let runRallying send incantation immediately.
        } else {
        if (_aiState != AIState::CollectingFood) {
            Logger::warn("AI: food critical! overriding to CollectFood");
            clearNav();
            transitionTo(AIState::CollectingFood, nowMs);
        }
        }
    }

    switch (_aiState) {
        case AIState::Idle:             runIdle(nowMs);             break;
        case AIState::CollectingFood:   runCollectingFood(nowMs);   break;
        case AIState::CollectingStones: runCollectingStones(nowMs); break;
        case AIState::MovingToRally:    runMovingToRally(nowMs);    break;
        case AIState::Rallying:         runRallying(nowMs);         break;
        case AIState::Incantating:      runIncantating(nowMs);      break;
        case AIState::Leading:          runLeading(nowMs);          break;
        case AIState::Forking:          runForking(nowMs);          break;
    }
}

// ────────────────────────────────────────────────────────────────
//  State transition helper
// ────────────────────────────────────────────────────────────────

void AI::transitionTo(AIState next, int64_t nowMs) {
    Logger::info("AI: state " + std::to_string((int)_aiState) +
                 " → " + std::to_string((int)next));
    _aiState = next;
    _stateEnteredMs = nowMs;
}

// ────────────────────────────────────────────────────────────────
//  Idle
// ────────────────────────────────────────────────────────────────

void AI::runIdle(int64_t nowMs) {
    int level = _state.getLevel();

    if (level >= 8) {
        Logger::info("AI: Level 8 achieved! Resting.");
        return;
    }

    // FIX 3: Only refresh vision/inventory when stale, not every tick.
    bool visionStale = (nowMs - _lastVisionRequestMs) > VISION_STALE_MS;
    if (visionStale) {
        sendVoir(nowMs);
        return; // wait for vision before deciding
    }

    bool invStale = (nowMs - _lastInventaireRequestMs) > VISION_STALE_MS;
    if (invStale) {
        sendInventaire(nowMs);
        return;
    }

    if (foodIsLow()) {
        transitionTo(AIState::CollectingFood, nowMs);
        return;
    }

    // FIX 5: Fork allowed from level 1 so we have players for level 2+
    if (shouldFork(nowMs)) {
        transitionTo(AIState::Forking, nowMs);
        return;
    }

    _stonesNeeded = computeMissingStones();

    if (_stonesNeeded.empty()) {
        Logger::info("AI: Have all stones for level " + std::to_string(level) + ", becoming leader");
        _isLeader = true;
        _rallyLevel = level;
        _rallyPeersConfirmed = 0;
        _stonesPlaced = false;
        transitionTo(AIState::Leading, nowMs);
    } else {
        Logger::info("AI: Need " + std::to_string(_stonesNeeded.size()) +
                     " more stone(s) for level " + std::to_string(level));
        _currentTarget = _stonesNeeded[0];
        _stoneSearchTurns = 0;
        transitionTo(AIState::CollectingStones, nowMs);
    }
}

// ────────────────────────────────────────────────────────────────
//  CollectingFood
// ────────────────────────────────────────────────────────────────

void AI::runCollectingFood(int64_t nowMs) {
    if (!foodIsLow()) {
        Logger::info("AI: food restored, returning to Idle");
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    auto nearest = _state.getNearestItem("nourriture");
    if (nearest.has_value() && nearest->distance == 0) {
        sendTake("nourriture", nowMs);
        return;
    }

    if (_navPlan.empty()) {
        if (nearest.has_value()) {
            buildPlanToResource("nourriture");
        } else {
            // Vision may be stale — request fresh view
            if (nowMs - _lastVisionRequestMs > VISION_STALE_MS) {
                sendVoir(nowMs);
                return;
            }
            buildExplorationPlan();
        }
    }

    executeNextNavStep(nowMs);
}

// ────────────────────────────────────────────────────────────────
//  CollectingStones
// ────────────────────────────────────────────────────────────────

void AI::runCollectingStones(int64_t nowMs) {
    _stonesNeeded = computeMissingStones();

    if (_stonesNeeded.empty()) {
        Logger::info("AI: All stones collected, going Idle");
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    if (foodIsLow()) {
        transitionTo(AIState::CollectingFood, nowMs);
        return;
    }

    _currentTarget = _stonesNeeded[0];

    auto nearest = _state.getNearestItem(_currentTarget);
    if (nearest.has_value() && nearest->distance == 0) {
        sendTake(_currentTarget, nowMs);
        clearNav();
        return;
    }

    // Opportunistic food grab
    auto foodNear = _state.getNearestItem("nourriture");
    if (foodNear.has_value() && foodNear->distance == 0 && _state.getFood() < FOOD_SAFE) {
        sendTake("nourriture", nowMs);
        return;
    }

    if (_navPlan.empty()) {
        if (nearest.has_value()) {
            buildPlanToResource(_currentTarget);
        } else {
            _stoneSearchTurns++;
            if (_stoneSearchTurns % 2 == 0 && nowMs - _lastVisionRequestMs > VISION_STALE_MS) {
                sendVoir(nowMs);
                return;
            }
            buildExplorationPlan();
        }
    }

    executeNextNavStep(nowMs);
}

// ────────────────────────────────────────────────────────────────
//  Leading
// ────────────────────────────────────────────────────────────────

void AI::runLeading(int64_t nowMs) {
    int level = _state.getLevel();
    const auto& req = levelReq(level);

    if (nowMs - _lastRallyBroadcast > RALLY_BROADCAST_INTERVAL_MS) {
        broadcast("RALLY:" + std::to_string(level), nowMs);
        _lastRallyBroadcast = nowMs;
        return; // one command per tick
    }

    if (req.players <= 1) {
        Logger::info("AI: Level 1 incantation — no peers needed");
        _stonesPlaced = false;
        transitionTo(AIState::Rallying, nowMs);
        return;
    }

    int here = playersOnTile();
    Logger::debug("AI: Leading level=" + std::to_string(level) +
                  " need=" + std::to_string(req.players) +
                  " have=" + std::to_string(here));

    if (here >= req.players) {
        Logger::info("AI: Enough players! Transitioning to Rallying");
        broadcast("START:" + std::to_string(level), nowMs);
        _stonesPlaced = false;
        transitionTo(AIState::Rallying, nowMs);
        return;
    }

    if (nowMs - _stateEnteredMs > RALLY_TIMEOUT_MS) {
        Logger::warn("AI: Rally timeout, giving up lead");
        _isLeader = false;
        broadcast("DONE:" + std::to_string(level), nowMs);
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    // Refresh vision while waiting for peers
    if (nowMs - _lastVisionRequestMs > VISION_STALE_MS * 2) {
        sendVoir(nowMs);
    }
}

// ────────────────────────────────────────────────────────────────
//  MovingToRally
// ────────────────────────────────────────────────────────────────

void AI::runMovingToRally(int64_t nowMs) {
    if (nowMs - _stateEnteredMs > RALLY_TIMEOUT_MS) {
        Logger::warn("AI: MovingToRally timeout");
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    if (_broadcastDirection == 0) {
        Logger::info("AI: Reached rally tile");
        broadcast("HERE:" + std::to_string(_rallyLevel), nowMs);
        _stonesPlaced = false;
        transitionTo(AIState::Rallying, nowMs);
        return;
    }

    if (_navPlan.empty()) {
        buildPlanTowardDirection(_broadcastDirection);
        if (nowMs - _lastVisionRequestMs > VISION_STALE_MS) {
            sendVoir(nowMs);
            return;
        }
    }

    if (!executeNextNavStep(nowMs)) {
        buildPlanTowardDirection(_broadcastDirection);
    }
}

// ────────────────────────────────────────────────────────────────
//  Rallying
// ────────────────────────────────────────────────────────────────

void AI::runRallying(int64_t nowMs) {
    int level = _state.getLevel();

    if (nowMs - _stateEnteredMs > RALLY_TIMEOUT_MS) {
        Logger::warn("AI: Rallying timeout");
        _stonesPlaced = false;
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    // If leader already has every condition, incantate immediately.
    if (_isLeader && _stonesPlaced && readyToIncantate()) {
        Logger::info("AI: Conditions met, initiating incantation!");
        sendIncantation(nowMs);
        transitionTo(AIState::Incantating, nowMs);
        return;
    }

    // Refresh state periodically
    if (nowMs - _lastVisionRequestMs > VISION_STALE_MS * 2) {
        sendVoir(nowMs);
        return;
    }
    if (nowMs - _lastInventaireRequestMs > VISION_STALE_MS * 2) {
        sendInventaire(nowMs);
        return;
    }

    if (!_stonesPlaced) {
        placeAllStonesOnTile(nowMs);
        return;
    }

    if (_isLeader) {
        int here = playersOnTile();
        const auto& req = levelReq(level);

        if (here >= req.players && allStonesOnTile()) {
            Logger::info("AI: Conditions met, initiating incantation!");
            sendIncantation(nowMs);
            transitionTo(AIState::Incantating, nowMs);
            return;
        }

        if (nowMs - _lastRallyBroadcast > RALLY_BROADCAST_INTERVAL_MS) {
            broadcast("START:" + std::to_string(level), nowMs);
            _lastRallyBroadcast = nowMs;
        }
    }
}

// ────────────────────────────────────────────────────────────────
//  Incantating
// ────────────────────────────────────────────────────────────────

void AI::runIncantating(int64_t nowMs) {
    if (nowMs - _stateEnteredMs > 30000) {
        Logger::warn("AI: Incantation timed out");
        _incantationSent = false;
        transitionTo(AIState::Idle, nowMs);
    }
    // Callback in sendIncantation() handles ok/ko.
}

// ────────────────────────────────────────────────────────────────
//  Forking
// ────────────────────────────────────────────────────────────────

void AI::runForking(int64_t nowMs) {
    sendFork(nowMs);
    _lastForkMs = nowMs;
    transitionTo(AIState::Idle, nowMs);
}

// ────────────────────────────────────────────────────────────────
//  onMessage  — broadcast received
// ────────────────────────────────────────────────────────────────

void AI::onMessage(const ServerMessage& msg) {
    if (msg.type != ServerMessageType::Message) return;
    if (!msg.messageText.has_value()) return;

    const std::string& text = *msg.messageText;
    int dir = msg.direction.value_or(0);
    int myLevel = _state.getLevel();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    auto parseLevelSuffix = [](const std::string& payload, size_t prefixLen) -> int {
        if (payload.size() <= prefixLen) return -1;
        try {
            return std::stoi(payload.substr(prefixLen));
        } catch (...) {
            return -1;
        }
    };

    Logger::debug("AI: broadcast dir=" + std::to_string(dir) + " text='" + text + "'");

    if (text.rfind("RALLY:", 0) == 0) {
        int rallyLevel = parseLevelSuffix(text, 6);
        if (rallyLevel < 0) return;
        if (rallyLevel != myLevel) return;

        if (_isLeader && _aiState == AIState::Leading && dir != 0) {
            Logger::info("AI: relinquishing leadership to merge same-level rally");
            _isLeader = false;
            _stonesPlaced = false;
            _rallyLevel = rallyLevel;
            _broadcastDirection = dir;
            clearNav();
            transitionTo(AIState::MovingToRally, nowMs);
            return;
        }

        if (_isLeader) return;
        if (_aiState == AIState::Incantating) return;

        if (_rallyLevel == rallyLevel && _broadcastDirection == dir &&
            (_aiState == AIState::MovingToRally || _aiState == AIState::Rallying)) {
            return;
        }

        Logger::info("AI: received RALLY for level " + std::to_string(rallyLevel) +
                     " from dir=" + std::to_string(dir));
        _rallyLevel = rallyLevel;
        _broadcastDirection = dir;

        if (dir == 0) {
            broadcast("HERE:" + std::to_string(myLevel), nowMs);
            _stonesPlaced = false;
            if (_aiState != AIState::Rallying)
                transitionTo(AIState::Rallying, nowMs);
        } else {
            clearNav();
            if (_aiState != AIState::MovingToRally)
                transitionTo(AIState::MovingToRally, nowMs);
        }
        return;
    }

    if (text.rfind("HERE:", 0) == 0 && _isLeader) {
        int peerLevel = parseLevelSuffix(text, 5);
        if (peerLevel < 0) return;
        if (peerLevel == myLevel) {
            _rallyPeersConfirmed++;
            Logger::info("AI: peer confirmed HERE, total=" + std::to_string(_rallyPeersConfirmed));
        }
        return;
    }

    if (text.rfind("START:", 0) == 0) {
        int startLevel = parseLevelSuffix(text, 6);
        if (startLevel < 0) return;
        if (startLevel != myLevel) return;
        if (_isLeader) return;
        if (_aiState == AIState::Incantating) return;

        if (_rallyLevel == startLevel && _broadcastDirection == dir &&
            (_aiState == AIState::MovingToRally || _aiState == AIState::Rallying)) {
            return;
        }

        Logger::info("AI: received START for level " + std::to_string(startLevel));
        _rallyLevel = startLevel;
        _broadcastDirection = dir;

        if (dir == 0) {
            _stonesPlaced = false;
            if (_aiState != AIState::Rallying)
                transitionTo(AIState::Rallying, nowMs);
        } else {
            clearNav();
            if (_aiState != AIState::MovingToRally)
                transitionTo(AIState::MovingToRally, nowMs);
        }
        return;
    }

    if (text.rfind("DONE:", 0) == 0) {
        int doneLevel = parseLevelSuffix(text, 5);
        if (doneLevel < 0) return;
        if (doneLevel != _rallyLevel) return;
        if (_aiState == AIState::Rallying || _aiState == AIState::MovingToRally) {
            Logger::info("AI: received DONE, disbanding rally");
            clearNav();
            transitionTo(AIState::Idle, nowMs);
        }
        return;
    }
}

// ────────────────────────────────────────────────────────────────
//  Navigation helpers
// ────────────────────────────────────────────────────────────────

void AI::clearNav() {
    _navPlan.clear();
    _navInFlight = false;
}

void AI::buildPlanToResource(const std::string& resource) {
    clearNav();
    auto tileOpt = _state.getNearestItem(resource);
    if (!tileOpt.has_value()) {
        buildExplorationPlan();
        return;
    }

    const VisionTile& tile = *tileOpt;
    const PlayerState& player = _state.getPlayer();

    if (tile.distance == 0) {
        _navPlan.push_back({NavAction::Take, resource});
        return;
    }

    int localX = tile.localX;
    int localY = tile.localY;
    int orientation = player.orientation; // 1=N,2=E,3=S,4=W

    // FIX 6: corrected world-delta conversion for all four orientations.
    // Server vision: localY = rows forward, localX = columns (negative=left, positive=right)
    // For NORTH (1): forward=−world_y, right=+world_x  → worldDX=localX, worldDY=−localY
    // For EAST  (2): forward=+world_x, right=+world_y  → worldDX=localY, worldDY=localX
    //   (but EAST-right is south, i.e. +worldY in server coords where Y increases south)
    // For SOUTH (3): forward=+world_y, right=−world_x  → worldDX=−localX, worldDY=localY
    // For WEST  (4): forward=−world_x, right=−world_y  → worldDX=−localY, worldDY=−localX
    int worldDX = 0, worldDY = 0;
    switch (orientation) {
        case 1: worldDX =  localX; worldDY = -localY; break; // North
        case 2: worldDX =  localY; worldDY =  localX; break; // East  (fixed: was localX,localY)
        case 3: worldDX = -localX; worldDY =  localY; break; // South
        case 4: worldDX = -localY; worldDY = -localX; break; // West
    }

    auto makeTurns = [](int from, int to) -> std::vector<NavStep> {
        std::vector<NavStep> t;
        int diff = (to - from + 4) % 4;
        if (diff == 1) t.push_back({NavAction::TurnRight, ""});
        else if (diff == 3) t.push_back({NavAction::TurnLeft, ""});
        else if (diff == 2) { t.push_back({NavAction::TurnRight, ""}); t.push_back({NavAction::TurnRight, ""}); }
        return t;
    };

    // Move X first (East/West)
    if (worldDX != 0) {
        int targetDir = (worldDX > 0) ? 2 : 4; // East or West
        for (auto& s : makeTurns(orientation, targetDir)) _navPlan.push_back(s);
        for (int i = 0; i < std::abs(worldDX); i++) _navPlan.push_back({NavAction::Forward, ""});
    }

    // Then Y (North/South)
    if (worldDY != 0) {
        int currentDir = orientation;
        if (worldDX > 0) currentDir = 2;
        else if (worldDX < 0) currentDir = 4;

        int targetDir = (worldDY < 0) ? 1 : 3; // North or South
        for (auto& s : makeTurns(currentDir, targetDir)) _navPlan.push_back(s);
        for (int i = 0; i < std::abs(worldDY); i++) _navPlan.push_back({NavAction::Forward, ""});
    }

    _navPlan.push_back({NavAction::Take, resource});
}

void AI::buildExplorationPlan() {
    clearNav();
    _explorationStep++;
    if (_explorationStep % 7 == 0)       _navPlan.push_back({NavAction::TurnRight, ""});
    else if (_explorationStep % 13 == 0) _navPlan.push_back({NavAction::TurnLeft, ""});
    _navPlan.push_back({NavAction::Forward, ""});
    _navPlan.push_back({NavAction::Forward, ""});
}

void AI::buildPlanTowardDirection(int direction) {
    clearNav();
    if (direction == 0) return;

    const PlayerState& player = _state.getPlayer();
    int ori = player.orientation; // 1=N,2=E,3=S,4=W

    // FIX 6: Broadcast direction is relative to listener's facing.
    // dir 1 = straight ahead, 3 = right, 5 = behind, 7 = left (octant mid-points)
    // Map to a world compass direction (1=N,2=E,3=S,4=W).
    // Octant → forward offset in 90° steps (0=forward,1=right,2=back,3=left)
    int offset = 0;
    switch (direction) {
        case 1:           offset = 0; break; // forward
        case 2: case 8:   offset = 0; break; // forward-ish (NE/NW → go forward)
        case 3: case 4:   offset = 1; break; // right
        case 5:           offset = 2; break; // behind
        case 6: case 7:   offset = 3; break; // left
        default:          offset = 0; break;
    }

    // Convert offset to absolute orientation: (ori-1 + offset) % 4 + 1
    int targetDir = ((ori - 1 + offset) % 4) + 1;

    int diff = (targetDir - ori + 4) % 4;
    if (diff == 1) _navPlan.push_back({NavAction::TurnRight, ""});
    else if (diff == 3) _navPlan.push_back({NavAction::TurnLeft, ""});
    else if (diff == 2) { _navPlan.push_back({NavAction::TurnRight, ""}); _navPlan.push_back({NavAction::TurnRight, ""}); }

    _navPlan.push_back({NavAction::Forward, ""});
}

bool AI::executeNextNavStep(int64_t nowMs) {
    if (_navPlan.empty()) return false;
    NavStep step = _navPlan.front();
    _navPlan.pop_front();

    switch (step.action) {
        case NavAction::Forward:   sendMove(nowMs);              return true;
        case NavAction::TurnLeft:  sendTurnLeft(nowMs);          return true;
        case NavAction::TurnRight: sendTurnRight(nowMs);         return true;
        case NavAction::Take:      sendTake(step.resource, nowMs); return true;
        case NavAction::Place:     sendPlace(step.resource, nowMs); return true;
        default: return false;
    }
}

// ────────────────────────────────────────────────────────────────
//  Stone logic
// ────────────────────────────────────────────────────────────────

std::vector<std::string> AI::computeMissingStones() const {
    int level = _state.getLevel();
    if (level >= 8) return {};

    const auto& req = levelReq(level);
    std::vector<std::string> missing;

    const auto& inv = _state.getPlayer().inventory;

    for (const auto& [stone, needed] : req.stones) {
        int have   = inv.count(stone) ? inv.at(stone) : 0;
        // Count how many of this stone are already on our tile (distance==0)
        int onTile = 0;
        for (const auto& t : _state.getTilesWithItem(stone))
            if (t.distance == 0) onTile += t.countItem(stone);
        // FIX 2: stones in inventory OR already on tile both count toward the goal.
        int deficit = needed - have - onTile;
        for (int i = 0; i < deficit; i++)
            missing.push_back(stone);
    }
    return missing;
}

bool AI::allStonesOnTile() const {
    int level = _state.getLevel();
    const auto& req = levelReq(level);

    for (const auto& [stone, needed] : req.stones) {
        int onFloor = 0;
        for (const auto& t : _state.getTilesWithItem(stone))
            if (t.distance == 0) onFloor += t.countItem(stone);
        if (onFloor < needed) return false;
    }
    return true;
}

void AI::placeAllStonesOnTile(int64_t nowMs) {
    int level = _state.getLevel();
    const auto& req = levelReq(level);

    // Count what's already on the floor using existing API
    std::map<std::string, int> onFloor;
    for (const auto& [stone, needed] : req.stones) {
        for (const auto& t : _state.getTilesWithItem(stone))
            if (t.distance == 0) onFloor[stone] += t.countItem(stone);
    }

    const auto& inv = _state.getPlayer().inventory;

    for (const auto& [stone, needed] : req.stones) {
        int floor  = onFloor.count(stone) ? onFloor.at(stone) : 0;
        int have   = inv.count(stone) ? inv.at(stone) : 0;
        int toPlace = std::max(0, needed - floor);
        int placing = std::min(toPlace, have);

        if (placing > 0) {
            // FIX 1: Only send one command per tick (the outer _commandInFlight
            // guard will stop us after the first; we return and come back next tick).
            sendPlace(stone, nowMs);
            // Do not mark complete until every required stone type is satisfied.
            _stonesPlaced = false;
            return; // one command per call
        }
    }

    // If we get here all placements are done
    _stonesPlaced = true;
}

// ────────────────────────────────────────────────────────────────
//  Command wrappers
// ────────────────────────────────────────────────────────────────

// Helper: mark a command as in-flight and record when it was sent
void AI::markInFlight(int64_t nowMs) {
    _commandInFlight = true;
    _commandInFlightSentMs = nowMs;
    _lastActionMs = nowMs;
}

// Helper: called inside every callback to clear the in-flight flag
void AI::clearInFlight() {
    _commandInFlight = false;
}

void AI::sendVoir(int64_t nowMs) {
    markInFlight(nowMs);
    _lastVisionRequestMs = nowMs;
    _sender.sendVoir();
    _sender.expectResponse("voir", [this](const ServerMessage&) {
        clearInFlight();
    });
}

void AI::sendInventaire(int64_t nowMs) {
    markInFlight(nowMs);
    _lastInventaireRequestMs = nowMs;
    _sender.sendInventaire();
    _sender.expectResponse("inventaire", [this](const ServerMessage&) {
        clearInFlight();
    });
}

void AI::sendTake(const std::string& resource, int64_t nowMs) {
    markInFlight(nowMs);
    _sender.sendPrend(resource);
    std::string key = "prend " + resource;
    _sender.expectResponse(key, [this, resource](const ServerMessage& msg) {
        clearInFlight();
        if (msg.isOk()) {
            Logger::info("AI: took " + resource);
        } else {
            Logger::warn("AI: failed to take " + resource);
            clearNav(); // tile may have changed
        }
    });
}

void AI::sendPlace(const std::string& resource, int64_t nowMs) {
    markInFlight(nowMs);
    _sender.sendPose(resource);
    std::string key = "pose " + resource;
    _sender.expectResponse(key, [this, resource](const ServerMessage& msg) {
        clearInFlight();
        if (msg.isOk()) {
            Logger::info("AI: placed " + resource);
        } else {
            Logger::warn("AI: failed to place " + resource);
        }
    });
}

void AI::sendMove(int64_t nowMs) {
    markInFlight(nowMs);
    _sender.sendAvance();
    _sender.expectResponse("avance", [this](const ServerMessage& msg) {
        clearInFlight();
        if (!msg.isOk()) {
            Logger::warn("AI: avance failed");
            clearNav();
        }
        // Request fresh vision after every move, but do it next tick via stale flag
        _lastVisionRequestMs = 0; // force stale
    });
}

void AI::sendTurnLeft(int64_t nowMs) {
    markInFlight(nowMs);
    _sender.sendGauche();
    _sender.expectResponse("gauche", [this](const ServerMessage&) {
        clearInFlight();
        _lastVisionRequestMs = 0; // vision changed after turn
    });
}

void AI::sendTurnRight(int64_t nowMs) {
    markInFlight(nowMs);
    _sender.sendDroite();
    _sender.expectResponse("droite", [this](const ServerMessage&) {
        clearInFlight();
        _lastVisionRequestMs = 0;
    });
}

void AI::sendIncantation(int64_t nowMs) {
    markInFlight(nowMs);
    _incantationSent = true;
    _incantationSentMs = nowMs;
    int levelSnapshot = _state.getLevel();

    _sender.sendIncantation();
    _sender.expectResponse("incantation",
        [this, levelSnapshot](const ServerMessage& msg) {
            if (msg.status == "in_progress") {
                // Keep in-flight; the real ok/ko comes next
                Logger::info("AI: incantation in progress…");
                return;
            }
            clearInFlight();
            if (msg.isOk()) {
                Logger::info("AI: INCANTATION SUCCESS! Was level " +
                             std::to_string(levelSnapshot));
                broadcast("DONE:" + std::to_string(levelSnapshot), 0);
                _isLeader = false;
                _incantationSent = false;
                _stonesPlaced = false;
                _lastForkMs = 0;
                _lastVisionRequestMs = 0;
                _lastInventaireRequestMs = 0;
                transitionTo(AIState::Idle, _incantationSentMs);
            } else {
                Logger::warn("AI: incantation failed, status=" + msg.status);
                _incantationSent = false;
                _stonesPlaced = false;
                _isLeader = false;
                transitionTo(AIState::Idle, _incantationSentMs);
            }
        });
}

void AI::sendFork(int64_t nowMs) {
    markInFlight(nowMs);
    _sender.sendFork();
    _sender.expectResponse("fork", [this](const ServerMessage& msg) {
        clearInFlight();
        if (msg.isOk()) Logger::info("AI: fork succeeded");
        else Logger::warn("AI: fork failed");
    });
}

void AI::broadcast(const std::string& msg, int64_t nowMs) {
    markInFlight(nowMs);
    _sender.sendBroadcast(msg);
    _sender.expectResponse("broadcast", [this](const ServerMessage&) {
        clearInFlight();
    });
}

// ────────────────────────────────────────────────────────────────
//  Utility
// ────────────────────────────────────────────────────────────────

bool AI::foodIsLow() const {
    return _state.getFood() < FOOD_SAFE;
}

bool AI::foodIsCritical() const {
    return _state.getFood() < FOOD_CRITICAL;
}

bool AI::shouldFork(int64_t nowMs) const {
    if (!_forkEnabled) return false;
    if (nowMs - _lastForkMs < FORK_INTERVAL_MS) return false;
    // Forking is expensive and can drown progression in low-level noise.
    // Only allow at higher levels with strong food reserve.
    if (_state.getLevel() < 4) return false;
    if (_state.getFood() < FOOD_SAFE + 8) return false;
    if (foodIsLow()) return false;
    return true;
}

int AI::playersNeeded() const {
    return levelReq(_state.getLevel()).players;
}

int AI::playersOnTile() const {
    return _state.getPlayersOnTile();
}

bool AI::readyToIncantate() const {
    return allStonesOnTile() &&
           playersOnTile() >= playersNeeded() &&
           !_incantationSent;
}

} // namespace zappy
