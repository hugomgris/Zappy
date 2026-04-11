#pragma once

#include "WorldState.hpp"
#include "CommandSender.hpp"
#include "NavigationPlanner.hpp"

#include <string>
#include <vector>
#include <queue>
#include <map>
#include <functional>
#include <sstream>

namespace zappy {

enum class AIState {
    Idle,
    WaitingForResponse,
    Exploring,
    Gathering,
    Returning,
    Incantating
};

class SimpleAI {
public:
    SimpleAI(WorldState& state, CommandSender& sender);
    ~SimpleAI() = default;

    void tick(int64_t nowMs);
    void onMessage(const ServerMessage& msg);

    void setForkEnabled(bool v)  { _forkEnabled = v; }
    void setTargetLevel(int v)   { _targetLevel = v; }
    void setMaxForks(int v)      { _maxForks = v; }

    AIState getState()          const { return _AIstate; }
    int     getForkCount()      const { return _forkCount; }
    size_t  getPendingActions() const { return _actionQueue.size(); }

private:
    // ------------------------------------------------------------------
    // tuning constants
    // ------------------------------------------------------------------
    static constexpr int64_t VOIR_INTERVAL_MS        = 300;   // how often to refresh vision
    static constexpr int64_t INVENTAIRE_INTERVAL_MS  = 1500;  // how often to refresh inventory
    static constexpr int64_t VISION_STALE_MS         = 2000;  // refuse to decide if vision is older
    static constexpr int64_t INCANTATION_TIMEOUT_MS  = 45000; // max wait as participant
    static constexpr int64_t INCANT_COOLDOWN_MS      = 3500;  // min ms between incantation attempts
    static constexpr int64_t FORK_COOLDOWN_MS        = 2000;  // min ms between forks
    static constexpr int64_t BROADCAST_INTERVAL_MS   = 1500;  // how often to re-broadcast

    // food thresholds
    int _foodEmergencyThreshold = 8;
    int _foodComfortThreshold   = 18;
    int _forkFoodThreshold      = 25;

    int  _commandTimeoutMs = 30000;

    // ------------------------------------------------------------------
    // config
    // ------------------------------------------------------------------
    bool _forkEnabled        = true;
    int  _targetLevel        = 8;
    int  _maxForks           = 5;
    bool _easyAscensionMode  = false;

    // ------------------------------------------------------------------
    // references
    // ------------------------------------------------------------------
    WorldState&     _state;
    CommandSender&  _sender;
    NavigationPlanner _planner;

    // ------------------------------------------------------------------
    // runtime state
    // ------------------------------------------------------------------
    AIState  _AIstate = AIState::Idle;

    // single "blocking" command tracking
    bool     _waitingForCmd   = false;
    uint64_t _pendingCommandId = 0;
    int64_t  _cmdSentAt        = 0;

    // timing
    int64_t _lastCommandTime      = 0;
    int64_t _lastVoirTime         = 0;
    int64_t _lastInventaireTime   = 0;
    int64_t _lastForkTime         = 0;
    int64_t _lastIncantationTime  = 0;
    int64_t _lastBroadcastTime    = 0;

    // counters
    int _forkCount = 0;

    // navigation
    std::queue<NavigationStep> _actionQueue;
    std::string                _currentResourceTarget;
    int                        _targetBroadcastDir = -1;
    int                        _clientTag = 0;
    int                        _selectedAnchorTag = -1;
    int64_t                    _anchorStickyUntil = 0;
    int64_t                    _lastAnchorFollowTime = 0;

    // ------------------------------------------------------------------
    // private methods
    // ------------------------------------------------------------------
    void decideNextAction(int64_t now);

    bool shouldGetFood()                const;
    bool shouldFork(int64_t now)        const;
    bool shouldIncantate(int64_t now)   const;

    std::vector<NavigationStep> buildStoneDropPlan() const;

    void startGathering(const std::string& resource, int64_t now);
    void startIncantation(int64_t now);
    void startFork(int64_t now);
    bool shouldLeadIncantation(int64_t now) const;

    void issueTrackedCommand(const std::string& key,
                             std::function<void(const ServerMessage&)> cb,
                             int64_t now);
    void executeNextAction(int64_t now);
    void onCommandComplete(const ServerMessage& msg);
};

} // namespace zappy
