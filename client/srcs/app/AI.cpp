/*
 * AI.cpp — Full cooperative AI for Zappy
 *
 * Design philosophy
 * ─────────────────
 * 1.  SURVIVAL FIRST – keep food above FOOD_SAFE whenever possible.
 *     Drop everything if food falls below FOOD_CRITICAL.
 *
 * 2.  STONE COLLECTION – after food is safe, figure out which stones
 *     are still needed for the current level's incantation and go get them.
 *     Stones must end up ON THE TILE (not in inventory) for the server to
 *     count them, so we place them just before incanting.
 *
 * 3.  COOPERATION via BROADCAST – when ready to incant the AI broadcasts
 *     RALLY:<level>.  Peers respond with HERE:<level> once they reach the
 *     tile.  The leader waits until enough same-level players are present,
 *     then broadcasts START:<level> and initiates incantation.
 *
 * 4.  FORKING – after every successful incantation (or periodically) we
 *     fork to grow team size and improve odds for higher-level incantations.
 *
 * Critical server facts (verified from game.c)
 * ─────────────────────────────────────────────
 * • Items for incantation must be on the TILE (t->items.*), not in inventory.
 * • All co-participants must be at the SAME LEVEL as the initiator.
 * • The server consumes items from the tile at incantation start.
 * • Vision distance = player level (a level-1 player sees 1 row ahead, etc.)
 * • Broadcast direction 0 = same tile, 1-8 = compass relative to listener.
 */

#include "AI.hpp"
#include "helpers/Logger.hpp"
#include "ProtocolTypes.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace zappy {

// ────────────────────────────────────────────────────────────────
//  Level requirement table  (matches server level_reqs[] exactly)
// ────────────────────────────────────────────────────────────────
// Level X→X+1:  players needed, stones needed ON TILE
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
//  tick()  – called every ~50 ms from networkLoop
// ────────────────────────────────────────────────────────────────

void AI::tick(int64_t nowMs) {
    // Global guard: don't spam commands faster than MOVE_COOLDOWN_MS
    if (nowMs - _lastActionMs < MOVE_COOLDOWN_MS)
        return;

    // State-level timeout guard (prevents getting stuck)
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

    // Food emergency overrides everything except active incantation
    if (_aiState != AIState::Incantating && foodIsCritical()) {
        if (_aiState != AIState::CollectingFood) {
            Logger::warn("AI: food critical! overriding to CollectFood");
            clearNav();
            transitionTo(AIState::CollectingFood, nowMs);
        }
    }

    switch (_aiState) {
        case AIState::Idle:           runIdle(nowMs);           break;
        case AIState::CollectingFood: runCollectingFood(nowMs); break;
        case AIState::CollectingStones: runCollectingStones(nowMs); break;
        case AIState::MovingToRally:  runMovingToRally(nowMs);  break;
        case AIState::Rallying:       runRallying(nowMs);       break;
        case AIState::Incantating:    runIncantating(nowMs);    break;
        case AIState::Leading:        runLeading(nowMs);        break;
        case AIState::Forking:        runForking(nowMs);        break;
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
//  Idle  – decide what to do next
// ────────────────────────────────────────────────────────────────

void AI::runIdle(int64_t nowMs) {
    int level = _state.getLevel();

    // Refresh vision first so we have good data to work with
    sendVoir(nowMs);
    sendInventaire(nowMs);

    if (level >= 8) {
        Logger::info("AI: Level 8 achieved! Resting.");
        return;
    }

    // Food check
    if (foodIsLow()) {
        transitionTo(AIState::CollectingFood, nowMs);
        return;
    }

    // Fork opportunity
    if (shouldFork(nowMs)) {
        transitionTo(AIState::Forking, nowMs);
        return;
    }

    // Compute missing stones
    _stonesNeeded = computeMissingStones();

    if (_stonesNeeded.empty()) {
        // We have all stones — become leader and rally
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

    // Is there food on the current tile?
    auto nearest = _state.getNearestItem("nourriture");
    if (nearest.has_value() && nearest->distance == 0) {
        sendTake("nourriture", nowMs);
        return;
    }

    // Build/continue navigation plan toward food
    if (_navPlan.empty()) {
        if (nearest.has_value()) {
            buildPlanToResource("nourriture");
        } else {
            buildExplorationPlan();
        }
        sendVoir(nowMs);
    }

    executeNextNavStep(nowMs);
}

// ────────────────────────────────────────────────────────────────
//  CollectingStones
// ────────────────────────────────────────────────────────────────

void AI::runCollectingStones(int64_t nowMs) {
    // Recalculate what we still need
    _stonesNeeded = computeMissingStones();

    if (_stonesNeeded.empty()) {
        Logger::info("AI: All stones collected, going Idle");
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    // Prioritize food if getting low
    if (foodIsLow()) {
        transitionTo(AIState::CollectingFood, nowMs);
        return;
    }

    _currentTarget = _stonesNeeded[0];

    auto nearest = _state.getNearestItem(_currentTarget);
    if (nearest.has_value() && nearest->distance == 0) {
        // On the tile — take it
        sendTake(_currentTarget, nowMs);
        clearNav();
        return;
    }

    // Also grab food opportunistically while exploring
    auto foodNear = _state.getNearestItem("nourriture");
    if (foodNear.has_value() && foodNear->distance == 0 && _state.getFood() < FOOD_SAFE) {
        sendTake("nourriture", nowMs);
        return;
    }

    if (_navPlan.empty()) {
        if (nearest.has_value()) {
            buildPlanToResource(_currentTarget);
        } else {
            // Explore and re-scan
            buildExplorationPlan();
            _stoneSearchTurns++;
            if (_stoneSearchTurns % 5 == 0) {
                sendVoir(nowMs);
                sendInventaire(nowMs);
                return;
            }
        }
    }

    executeNextNavStep(nowMs);
}

// ────────────────────────────────────────────────────────────────
//  Leading  – broadcast RALLY and wait for peers
// ────────────────────────────────────────────────────────────────

void AI::runLeading(int64_t nowMs) {
    int level = _state.getLevel();
    const auto& req = levelReq(level);

    // Periodically re-broadcast RALLY
    if (nowMs - _lastRallyBroadcast > RALLY_BROADCAST_INTERVAL_MS) {
        broadcast("RALLY:" + std::to_string(level), nowMs);
        _lastRallyBroadcast = nowMs;
    }

    // If level 1, no peers needed — proceed immediately
    if (req.players <= 1) {
        Logger::info("AI: Level 1 incantation — no peers needed");
        _stonesPlaced = false;
        transitionTo(AIState::Rallying, nowMs);
        return;
    }

    // Check if we have enough peers on our tile
    int here = playersOnTile(); // includes ourselves
    Logger::debug("AI: Leading for level " + std::to_string(level) +
                  " need " + std::to_string(req.players) +
                  " have " + std::to_string(here));

    if (here >= req.players) {
        Logger::info("AI: Enough players! Transitioning to Rallying");
        broadcast("START:" + std::to_string(level), nowMs);
        _stonesPlaced = false;
        transitionTo(AIState::Rallying, nowMs);
        return;
    }

    // Rally timeout — give up leading, go back to stone collection
    if (nowMs - _stateEnteredMs > RALLY_TIMEOUT_MS) {
        Logger::warn("AI: Rally timeout, giving up lead");
        _isLeader = false;
        broadcast("DONE:" + std::to_string(level), nowMs);
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    // While waiting, keep scanning so we know when peers arrive
    if (nowMs - _lastActionMs > 2000) {
        sendVoir(nowMs);
        sendInventaire(nowMs);
    }
}

// ────────────────────────────────────────────────────────────────
//  MovingToRally  – following a broadcast toward a leader
// ────────────────────────────────────────────────────────────────

void AI::runMovingToRally(int64_t nowMs) {
    // If we got here, _broadcastDirection was set by onMessage
    if (nowMs - _stateEnteredMs > RALLY_TIMEOUT_MS) {
        Logger::warn("AI: MovingToRally timeout");
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    // Check if we are on the right tile (direction=0 means same tile)
    if (_broadcastDirection == 0) {
        Logger::info("AI: Reached rally tile");
        broadcast("HERE:" + std::to_string(_rallyLevel), nowMs);
        _stonesPlaced = false;
        transitionTo(AIState::Rallying, nowMs);
        return;
    }

    if (_navPlan.empty()) {
        buildPlanTowardDirection(_broadcastDirection);
        // After moving one step, update direction via a fresh broadcast listener
        // (direction will update in onMessage)
        sendVoir(nowMs);
    }

    if (!executeNextNavStep(nowMs)) {
        buildPlanTowardDirection(_broadcastDirection);
    }
}

// ────────────────────────────────────────────────────────────────
//  Rallying  – on the tile, waiting for the leader's START signal
//              and placing stones
// ────────────────────────────────────────────────────────────────

void AI::runRallying(int64_t nowMs) {
    int level = _state.getLevel();

    if (nowMs - _stateEnteredMs > RALLY_TIMEOUT_MS) {
        Logger::warn("AI: Rallying timeout");
        _stonesPlaced = false;
        transitionTo(AIState::Idle, nowMs);
        return;
    }

    // Keep scanning
    sendVoir(nowMs);
    sendInventaire(nowMs);

    // Place stones onto the tile if we haven't yet
    if (!_stonesPlaced) {
        placeAllStonesOnTile(nowMs);
        // placeAllStonesOnTile returns once commands are queued
        // Mark true only if we don't have any stones to place
        if (computeMissingStones().size() == _stonesNeeded.size()) {
            // No change; we either already placed or have nothing
            _stonesPlaced = true;
        }
        return;
    }

    // If we are the leader, check conditions and fire incantation
    if (_isLeader) {
        int here = playersOnTile();
        const auto& req = levelReq(level);

        if (here >= req.players && allStonesOnTile()) {
            Logger::info("AI: Conditions met, initiating incantation!");
            sendIncantation(nowMs);
            transitionTo(AIState::Incantating, nowMs);
            return;
        }

        // Periodically re-broadcast START to keep peers awake
        if (nowMs - _lastRallyBroadcast > RALLY_BROADCAST_INTERVAL_MS) {
            broadcast("START:" + std::to_string(level), nowMs);
            _lastRallyBroadcast = nowMs;
        }
    }
    // Non-leader: just wait. If a long time passes without incantation, bail.
}

// ────────────────────────────────────────────────────────────────
//  Incantating  – waiting for ok/ko response
// ────────────────────────────────────────────────────────────────

void AI::runIncantating(int64_t nowMs) {
    // Timeout safety — incantation delay from game.c is m_incantation_time
    // Give a generous 30 s
    if (nowMs - _stateEnteredMs > 30000) {
        Logger::warn("AI: Incantation timed out");
        _incantationSent = false;
        transitionTo(AIState::Idle, nowMs);
    }
    // Otherwise just wait — the callback in sendIncantation() will fire
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
//  onMessage  – broadcast received
// ────────────────────────────────────────────────────────────────

void AI::onMessage(const ServerMessage& msg) {
    if (msg.type != ServerMessageType::Message) return;
    if (!msg.messageText.has_value()) return;

    const std::string& text = *msg.messageText;
    int dir = msg.direction.value_or(0);
    int myLevel = _state.getLevel();

    Logger::debug("AI: broadcast dir=" + std::to_string(dir) + " text='" + text + "'");

    // ── RALLY:<level> ────────────────────────────────────────────
    if (text.substr(0, 6) == "RALLY:") {
        int rallyLevel = std::stoi(text.substr(6));
        if (rallyLevel != myLevel) return; // not for us
        if (_isLeader) return;              // we are already leading
        if (_aiState == AIState::Incantating) return;

        Logger::info("AI: received RALLY for level " + std::to_string(rallyLevel) +
                     " from dir=" + std::to_string(dir));

        _rallyLevel = rallyLevel;
        _broadcastDirection = dir;

        if (dir == 0) {
            // Already on the same tile as the leader
            broadcast("HERE:" + std::to_string(myLevel), 0);
            _stonesPlaced = false;
            transitionTo(AIState::Rallying, _stateEnteredMs); // keep time
        } else {
            clearNav();
            transitionTo(AIState::MovingToRally, _stateEnteredMs);
        }
        return;
    }

    // ── HERE:<level>  (leader hears this from a peer) ────────────
    if (text.substr(0, 5) == "HERE:" && _isLeader) {
        int peerLevel = std::stoi(text.substr(5));
        if (peerLevel == myLevel) {
            _rallyPeersConfirmed++;
            Logger::info("AI: peer confirmed HERE, total=" +
                         std::to_string(_rallyPeersConfirmed));
        }
        return;
    }

    // ── START:<level> ────────────────────────────────────────────
    if (text.substr(0, 6) == "START:") {
        int startLevel = std::stoi(text.substr(6));
        if (startLevel != myLevel) return;
        if (_isLeader) return;
        if (_aiState == AIState::Incantating) return;

        Logger::info("AI: received START for level " + std::to_string(startLevel));
        _broadcastDirection = dir;

        if (dir == 0) {
            // On the tile — place stones and get ready
            _stonesPlaced = false;
            transitionTo(AIState::Rallying, _stateEnteredMs);
        } else {
            clearNav();
            transitionTo(AIState::MovingToRally, _stateEnteredMs);
        }
        return;
    }

    // ── DONE:<level> ─────────────────────────────────────────────
    if (text.substr(0, 5) == "DONE:") {
        if (_aiState == AIState::Rallying || _aiState == AIState::MovingToRally) {
            Logger::info("AI: received DONE, disbanding rally");
            clearNav();
            transitionTo(AIState::Idle, _stateEnteredMs);
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

// Build a step-by-step plan to a nearby tile that has the resource
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
    int orientation = player.orientation;

    // Convert local vision coordinates to world-relative delta
    // Vision is always relative to current facing:
    // localY = distance forward, localX = lateral offset
    // We need to turn so our +Y axis aligns with the target
    int worldDX = 0, worldDY = 0;
    switch (orientation) {
        case 1: worldDX =  localX; worldDY = -localY; break; // North
        case 2: worldDX =  localY; worldDY =  localX; break; // East (server: EAST=1)
        case 3: worldDX = -localX; worldDY =  localY; break; // South
        case 4: worldDX = -localY; worldDY = -localX; break; // West
    }

    // Move X first
    if (worldDX != 0) {
        int targetDir = (worldDX > 0) ? 2 : 4; // East or West
        auto turns = [&]() -> std::vector<NavStep> {
            std::vector<NavStep> t;
            int diff = (targetDir - orientation + 4) % 4;
            if (diff == 1) t.push_back({NavAction::TurnRight, ""});
            else if (diff == 3) t.push_back({NavAction::TurnLeft, ""});
            else if (diff == 2) { t.push_back({NavAction::TurnRight, ""}); t.push_back({NavAction::TurnRight, ""}); }
            return t;
        }();
        for (auto& s : turns) _navPlan.push_back(s);
        for (int i = 0; i < std::abs(worldDX); i++) _navPlan.push_back({NavAction::Forward, ""});
    }

    // Move Y
    if (worldDY != 0) {
        int currentDir = orientation;
        if (worldDX > 0) currentDir = 2;
        else if (worldDX < 0) currentDir = 4;

        int targetDir = (worldDY < 0) ? 1 : 3; // North or South
        auto turns = [&]() -> std::vector<NavStep> {
            std::vector<NavStep> t;
            int diff = (targetDir - currentDir + 4) % 4;
            if (diff == 1) t.push_back({NavAction::TurnRight, ""});
            else if (diff == 3) t.push_back({NavAction::TurnLeft, ""});
            else if (diff == 2) { t.push_back({NavAction::TurnRight, ""}); t.push_back({NavAction::TurnRight, ""}); }
            return t;
        }();
        for (auto& s : turns) _navPlan.push_back(s);
        for (int i = 0; i < std::abs(worldDY); i++) _navPlan.push_back({NavAction::Forward, ""});
    }

    // Take at destination
    _navPlan.push_back({NavAction::Take, resource});
}

void AI::buildExplorationPlan() {
    clearNav();
    _explorationStep++;

    // Spiral-ish pattern: mostly forward, occasionally turn
    if (_explorationStep % 7 == 0) {
        _navPlan.push_back({NavAction::TurnRight, ""});
    } else if (_explorationStep % 13 == 0) {
        _navPlan.push_back({NavAction::TurnLeft, ""});
    }

    // Always move forward
    _navPlan.push_back({NavAction::Forward, ""});
    _navPlan.push_back({NavAction::Forward, ""});
}

void AI::buildPlanTowardDirection(int direction) {
    clearNav();
    const PlayerState& player = _state.getPlayer();

    // direction: 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW (relative to listener)
    // We need to turn to face that direction relative to our current orientation
    // First map broadcast direction to a world compass direction
    // (broadcast dir is relative to listener's facing)
    // orientation: 1=N, 2=E, 3=S, 4=W  (server's NORTH=0 stored as 1 in PlayerState)

    if (direction == 0) return; // same tile

    // Map dir 1-8 to a heading offset (in 45-deg steps) from forward
    // Forward=dir1=0offset, NE=dir2=45, E=dir3=90, etc.
    // We simplify to cardinal directions
    int targetWorldDir = player.orientation; // default: go forward
    switch (direction) {
        case 1: targetWorldDir = player.orientation; break; // forward (N relative)
        case 2: // NE – go N (forward)
        case 8: targetWorldDir = player.orientation; break;
        case 3: // E (right)
        case 4: { // SE
            targetWorldDir = player.orientation % 4 + 1;
            break;
        }
        case 5: { // S (backward – turn 180)
            targetWorldDir = (player.orientation - 1 + 2) % 4 + 1;
            break;
        }
        case 6: // SW
        case 7: { // W (left)
            targetWorldDir = (player.orientation + 2) % 4 + 1;
            if (targetWorldDir == 0) targetWorldDir = 4;
            break;
        }
    }

    // Turn to face targetWorldDir
    int diff = (targetWorldDir - player.orientation + 4) % 4;
    if (diff == 1) _navPlan.push_back({NavAction::TurnRight, ""});
    else if (diff == 3) _navPlan.push_back({NavAction::TurnLeft, ""});
    else if (diff == 2) {
        _navPlan.push_back({NavAction::TurnRight, ""});
        _navPlan.push_back({NavAction::TurnRight, ""});
    }

    _navPlan.push_back({NavAction::Forward, ""});
}

bool AI::executeNextNavStep(int64_t nowMs) {
    if (_navPlan.empty()) return false;

    NavStep step = _navPlan.front();
    _navPlan.pop_front();

    switch (step.action) {
        case NavAction::Forward:   sendMove(nowMs);             return true;
        case NavAction::TurnLeft:  sendTurnLeft(nowMs);         return true;
        case NavAction::TurnRight: sendTurnRight(nowMs);        return true;
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

    for (const auto& [stone, needed] : req.stones) {
        const auto& inv = _state.getPlayer().inventory;
        int have = inv.count(stone) ? inv.at(stone) : 0;
        // Count what's already on our current tile (vision tile 0)
        int onFloor = 0;
        auto tiles = _state.getTilesWithItem(stone);
        for (const auto& t : tiles) {
            if (t.distance == 0) onFloor += t.countItem(stone);
        }
        int deficit = needed - have - onFloor;
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
        auto tiles = _state.getTilesWithItem(stone);
        for (const auto& t : tiles) {
            if (t.distance == 0) onFloor += t.countItem(stone);
        }
        if (onFloor < needed) return false;
    }
    return true;
}

void AI::placeAllStonesOnTile(int64_t nowMs) {
    // Place every stone in inventory that is needed for this incantation
    int level = _state.getLevel();
    const auto& req = levelReq(level);

    for (const auto& [stone, needed] : req.stones) {
        // How many already on floor?
        int onFloor = 0;
        auto tiles = _state.getTilesWithItem(stone);
        for (const auto& t : tiles) {
            if (t.distance == 0) onFloor += t.countItem(stone);
        }

        int toPlace = needed - onFloor;
        const auto& inv = _state.getPlayer().inventory;
        int have = inv.count(stone) ? inv.at(stone) : 0;
        int placing = std::min(toPlace, have);

        for (int i = 0; i < placing; i++) {
            sendPlace(stone, nowMs);
        }
    }
    _stonesPlaced = true;
}

// ────────────────────────────────────────────────────────────────
//  Command wrappers – each registers a callback for the response
// ────────────────────────────────────────────────────────────────

void AI::sendVoir(int64_t nowMs) {
    (void)nowMs;
    _sender.sendVoir();
    _sender.expectResponse("voir", [](const ServerMessage&) {});
    _lastActionMs = nowMs;
}

void AI::sendInventaire(int64_t nowMs) {
    (void)nowMs;
    _sender.sendInventaire();
    _sender.expectResponse("inventaire", [](const ServerMessage&) {});
    _lastActionMs = nowMs;
}

void AI::sendTake(const std::string& resource, int64_t nowMs) {
    _sender.sendPrend(resource);
    std::string key = "prend " + resource;
    _sender.expectResponse(key, [this, resource](const ServerMessage& msg) {
        if (msg.isOk()) {
            Logger::info("AI: took " + resource);
        } else {
            Logger::warn("AI: failed to take " + resource);
        }
    });
    _lastActionMs = nowMs;
}

void AI::sendPlace(const std::string& resource, int64_t nowMs) {
    _sender.sendPose(resource);
    std::string key = "pose " + resource;
    _sender.expectResponse(key, [this, resource](const ServerMessage& msg) {
        if (msg.isOk()) {
            Logger::info("AI: placed " + resource);
        } else {
            Logger::warn("AI: failed to place " + resource);
        }
    });
    _lastActionMs = nowMs;
}

void AI::sendMove(int64_t nowMs) {
    _sender.sendAvance();
    _sender.expectResponse("avance", [this](const ServerMessage& msg) {
        if (!msg.isOk()) Logger::warn("AI: avance failed");
        // Request fresh vision after every move
        _sender.sendVoir();
        _sender.expectResponse("voir", [](const ServerMessage&) {});
    });
    _lastActionMs = nowMs;
}

void AI::sendTurnLeft(int64_t nowMs) {
    _sender.sendGauche();
    _sender.expectResponse("gauche", [](const ServerMessage&) {});
    _lastActionMs = nowMs;
}

void AI::sendTurnRight(int64_t nowMs) {
    _sender.sendDroite();
    _sender.expectResponse("droite", [](const ServerMessage&) {});
    _lastActionMs = nowMs;
}

void AI::sendIncantation(int64_t nowMs) {
    _incantationSent = true;
    _incantationSentMs = nowMs;
    int levelSnapshot = _state.getLevel();

    _sender.sendIncantation();
    _sender.expectResponse("incantation",
        [this, levelSnapshot](const ServerMessage& msg) {
            if (msg.status == "in_progress") {
                Logger::info("AI: incantation in progress…");
                return; // stay in Incantating
            }
            if (msg.isOk()) {
                Logger::info("AI: INCANTATION SUCCESS! Was level " +
                             std::to_string(levelSnapshot));
                broadcast("DONE:" + std::to_string(levelSnapshot), 0);
                _isLeader = false;
                _incantationSent = false;
                _stonesPlaced = false;
                _lastForkMs = 0; // allow immediate fork after level-up
                transitionTo(AIState::Idle, _incantationSentMs);
            } else {
                Logger::warn("AI: incantation failed, status=" + msg.status);
                _incantationSent = false;
                _stonesPlaced = false;
                _isLeader = false;
                transitionTo(AIState::Idle, _incantationSentMs);
            }
        });
    _lastActionMs = nowMs;
}

void AI::sendFork(int64_t nowMs) {
    _sender.sendFork();
    _sender.expectResponse("fork", [](const ServerMessage& msg) {
        if (msg.isOk()) Logger::info("AI: fork succeeded");
        else Logger::warn("AI: fork failed");
    });
    _lastActionMs = nowMs;
}

void AI::broadcast(const std::string& msg, int64_t nowMs) {
    _sender.sendBroadcast(msg);
    _sender.expectResponse("broadcast", [](const ServerMessage&) {});
    _lastActionMs = nowMs;
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
    // Fork if team needs higher-level incantations (more players useful from level 2+)
    int level = _state.getLevel();
    if (level < 2) return false; // don't fork at level 1 (slow the game)
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
