#pragma once

#include "ProtocolTypes.hpp"
#include "WorldState.hpp"
#include "CommandSender.hpp"

#include <string>
#include <vector>
#include <functional>
#include <map>
#include <optional>
#include <deque>
#include <chrono>

namespace zappy {

// ────────────────────────────────────────────────────────────────
//  Broadcast protocol tags
// ────────────────────────────────────────────────────────────────
//
//  Every broadcast message starts with a tag so players can
//  cooperate without confusing messages.
//
//  RALLY:<level>        – "I need more level-<level> players here"
//  HERE:<level>         – "I am level-<level> and I am on the rally tile"
//  START:<level>        – Leader announces incantation is about to start
//  DONE:<level>         – Incantation complete, rally disbands
//  STONES:<level>       – "I have stones and am moving to the rally"
//  PING                 – keepalive / team-presence check
// ────────────────────────────────────────────────────────────────

// ────────────────────────────────────────────────────────────────
//  State machine
// ────────────────────────────────────────────────────────────────

enum class AIState {
    Idle,           // just started / just finished something
    CollectingFood, // food critically low
    CollectingStones, // gathering missing stones for current level
    MovingToRally,  // navigating toward a rally point broadcast
    Rallying,       // on the rally tile, waiting for peers + placing stones
    Incantating,    // incantation in flight
    Leading,        // we are the rally leader, broadcasting RALLY
    Forking,        // sending fork to increase team size
};

// ────────────────────────────────────────────────────────────────
//  Level requirements (mirrors server exactly)
// ────────────────────────────────────────────────────────────────

struct LevelReq {
    int players;
    std::map<std::string, int> stones; // must be ON THE TILE (not in inventory)
};

// ────────────────────────────────────────────────────────────────
//  Pending navigation plan
// ────────────────────────────────────────────────────────────────

enum class NavAction { Forward, TurnLeft, TurnRight, Take, Place, Wait };

struct NavStep {
    NavAction action;
    std::string resource; // for Take/Place
};

// ────────────────────────────────────────────────────────────────
//  AI class
// ────────────────────────────────────────────────────────────────

class AI {
public:
    AI(WorldState& state, CommandSender& sender);
    ~AI() = default;

    // Called every network-loop iteration (~50 ms)
    void tick(int64_t nowMs);

    // Configuration (can be called before or after run())
    void setForkEnabled(bool enabled) { _forkEnabled = enabled; }

    // Called when a broadcast message arrives
    void onMessage(const ServerMessage& msg);

    // Called when a non-response server event arrives (level-up, death …)
    void onEvent(const ServerMessage& msg) { (void)msg; }

    // Called when a command response is dispatched to us by CommandSender
    // (we register callbacks per-command; this is the catch-all for events)
    void onCommandComplete(const ServerMessage& msg) { (void)msg; }

private:
    // ── references ──────────────────────────────────────────────
    WorldState&    _state;
    CommandSender& _sender;

    // ── state machine ───────────────────────────────────────────
    AIState _aiState      = AIState::Idle;
    bool    _cmdInFlight  = false;   // true while we are waiting for a response
    int64_t _lastActionMs = 0;
    int64_t _stateEnteredMs = 0;

    // ── navigation ──────────────────────────────────────────────
    std::deque<NavStep> _navPlan;
    int  _explorationStep = 0;
    bool _navInFlight     = false;

    // ── collecting stones ────────────────────────────────────────
    std::vector<std::string> _stonesNeeded;   // what we still need to collect
    std::string              _currentTarget;  // stone type we are chasing right now
    int                      _stoneSearchTurns = 0;

    // ── food ────────────────────────────────────────────────────
    static constexpr int FOOD_CRITICAL  = 5;
    static constexpr int FOOD_SAFE      = 15;

    // ── rallying / cooperation ───────────────────────────────────
    bool    _isLeader          = false;
    int     _rallyLevel        = 0;      // level we are rallying for
    int     _rallyPeersConfirmed = 0;
    int64_t _lastRallyBroadcast = 0;
    int64_t _rallyTimeout       = 0;     // give up rallying after this
    int     _broadcastDirection  = 0;    // direction of the RALLY broadcast we received
    bool    _stonesPlaced        = false; // have we placed our stones on the tile?

    // ── incantation ─────────────────────────────────────────────
    bool    _incantationSent   = false;
    int64_t _incantationSentMs = 0;

    // ── forking ─────────────────────────────────────────────────
    bool    _forkEnabled       = true;
    int64_t _lastForkMs        = 0;
    static constexpr int64_t FORK_INTERVAL_MS = 30000; // fork every 30 s if slots free

    // ── timing ──────────────────────────────────────────────────
    static constexpr int64_t CMD_TIMEOUT_MS     = 8000;
    static constexpr int64_t STATE_TIMEOUT_MS   = 20000;
    static constexpr int64_t RALLY_TIMEOUT_MS   = 30000;
    static constexpr int64_t RALLY_BROADCAST_INTERVAL_MS = 3000;
    static constexpr int64_t MOVE_COOLDOWN_MS   = 200;   // don't spam moves

    // ── static level requirement table ──────────────────────────
    static const LevelReq& levelReq(int level);

    // ── high-level state transitions ────────────────────────────
    void transitionTo(AIState next, int64_t nowMs);
    void runIdle(int64_t nowMs);
    void runCollectingFood(int64_t nowMs);
    void runCollectingStones(int64_t nowMs);
    void runMovingToRally(int64_t nowMs);
    void runRallying(int64_t nowMs);
    void runIncantating(int64_t nowMs);
    void runLeading(int64_t nowMs);
    void runForking(int64_t nowMs);

    // ── navigation helpers ───────────────────────────────────────
    void buildPlanToResource(const std::string& resource);
    void buildExplorationPlan();
    void buildPlanTowardDirection(int direction);
    bool executeNextNavStep(int64_t nowMs);    // returns true if step dispatched
    void clearNav();

    // ── command helpers ──────────────────────────────────────────
    void sendVoir(int64_t nowMs);
    void sendInventaire(int64_t nowMs);
    void sendTake(const std::string& resource, int64_t nowMs);
    void sendPlace(const std::string& resource, int64_t nowMs);
    void sendMove(int64_t nowMs);
    void sendTurnLeft(int64_t nowMs);
    void sendTurnRight(int64_t nowMs);
    void sendIncantation(int64_t nowMs);
    void sendFork(int64_t nowMs);
    void broadcast(const std::string& msg, int64_t nowMs);

    // ── stone logic ──────────────────────────────────────────────
    std::vector<std::string> computeMissingStones() const;
    bool allStonesOnTile() const;
    void placeAllStonesOnTile(int64_t nowMs);

    // ── misc ─────────────────────────────────────────────────────
    bool foodIsLow() const;
    bool foodIsCritical() const;
    bool shouldFork(int64_t nowMs) const;
    int  playersNeeded() const;
    int  playersOnTile() const;
    bool readyToIncantate() const;
};

} // namespace zappy
