#pragma once

/*
 * AI.hpp — Fixed header
 *
 * New fields vs original:
 *   _commandInFlight        — true while waiting for a server response
 *   _commandInFlightSentMs  — when the in-flight command was sent (for timeout)
 *   _lastVisionRequestMs    — last time we sent a voir (staleness gate)
 *   _lastInventaireRequestMs — same for inventaire
 *
 * New constants:
 *   COMMAND_FLIGHT_TIMEOUT_MS — safety valve to clear a stuck in-flight flag
 *   VISION_STALE_MS           — how long before we re-request vision/inventory
 *
 * shouldFork() now has no level gate (fork allowed at level 1).
 */

#include "WorldState.hpp"
#include "CommandSender.hpp"
#include "ProtocolTypes.hpp"

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace zappy {

enum class AIState {
    Idle,
    CollectingFood,
    CollectingStones,
    MovingToRally,
    Rallying,
    Incantating,
    Leading,
    Forking,
};

enum class NavAction {
    Forward,
    TurnLeft,
    TurnRight,
    Take,
    Place,
    None,
};

struct NavStep {
    NavAction   action;
    std::string resource;
};

struct LevelReq {
    int players;
    std::map<std::string, int> stones;
};

class AI {
public:
    AI(WorldState& state, CommandSender& sender);

    void tick(int64_t nowMs);
    void onMessage(const ServerMessage& msg);
    void setForkEnabled(bool enabled) { _forkEnabled = enabled; }

    // Constants — tweak as needed
    static constexpr int FOOD_SAFE               = 12;
    static constexpr int FOOD_CRITICAL           = 4;
    static constexpr int MOVE_COOLDOWN_MS        = 50;   // kept for back-compat; not used in hot path anymore
    static constexpr int RALLY_BROADCAST_INTERVAL_MS = 5000;
    static constexpr int RALLY_TIMEOUT_MS        = 45000;
    static constexpr int FORK_INTERVAL_MS        = 60000;
    static constexpr int STATE_TIMEOUT_MS        = 180000;
    static constexpr int COMMAND_FLIGHT_TIMEOUT_MS = 10000; // NEW: safety valve
    static constexpr int VISION_STALE_MS         = 1500;    // NEW: re-request vision after this

private:
    WorldState&    _state;
    CommandSender& _sender;

    AIState _aiState        = AIState::Idle;
    int64_t _stateEnteredMs = 0;
    int64_t _lastActionMs   = 0;

    // FIX 1: Command serialization
    bool    _commandInFlight        = false;
    int64_t _commandInFlightSentMs  = 0;
    int64_t _lastVisionRequestMs    = 0;
    int64_t _lastInventaireRequestMs = 0;

    // Navigation
    std::deque<NavStep> _navPlan;
    bool _navInFlight   = false;
    int  _explorationStep = 0;

    // Stone collection
    std::vector<std::string> _stonesNeeded;
    std::string _currentTarget;
    int _stoneSearchTurns = 0;

    // Incantation / rally
    bool    _isLeader              = false;
    int     _rallyLevel            = 0;
    int     _rallyPeersConfirmed   = 0;
    int     _broadcastDirection    = 0;
    bool    _stonesPlaced          = false;
    bool    _incantationSent       = false;
    int64_t _incantationSentMs     = 0;
    int64_t _lastRallyBroadcast    = 0;

    // Forking
    bool    _forkEnabled  = true;
    int64_t _lastForkMs   = 0;

    // State runners
    void runIdle(int64_t nowMs);
    void runCollectingFood(int64_t nowMs);
    void runCollectingStones(int64_t nowMs);
    void runMovingToRally(int64_t nowMs);
    void runRallying(int64_t nowMs);
    void runIncantating(int64_t nowMs);
    void runLeading(int64_t nowMs);
    void runForking(int64_t nowMs);

    void transitionTo(AIState next, int64_t nowMs);

    // Navigation
    void clearNav();
    void buildPlanToResource(const std::string& resource);
    void buildExplorationPlan();
    void buildPlanTowardDirection(int direction);
    bool executeNextNavStep(int64_t nowMs);

    // Stone helpers
    std::vector<std::string> computeMissingStones() const;
    bool allStonesOnTile() const;
    void placeAllStonesOnTile(int64_t nowMs);

    // Command wrappers
    void markInFlight(int64_t nowMs);
    void clearInFlight();

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

    // Utility
    bool foodIsLow() const;
    bool foodIsCritical() const;
    bool shouldFork(int64_t nowMs) const;
    int  playersNeeded() const;
    int  playersOnTile() const;
    bool readyToIncantate() const;

    static const LevelReq& levelReq(int level);
};

} // namespace zappy
