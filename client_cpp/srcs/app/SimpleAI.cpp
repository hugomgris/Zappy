/*
 * SimpleAI.cpp — full rewrite
 *
 * Key design decisions / bugs fixed vs previous version:
 *
 * 1. FIRE-AND-FORGET vs TRACKED commands
 *    voir and inventaire are sent without expectResponse so they NEVER
 *    consume a pending slot. The AI was blocking on them indefinitely.
 *
 * 2. SINGLE pending-command slot
 *    The AI tracks exactly one "blocking" command at a time (_waitingForCmd).
 *    All decision logic waits only on that slot, not on the generic sender queue.
 *
 * 3. onCommandComplete no longer calls _state.onResponse
 *    Client.cpp already routes every ServerMessage through WorldState::onResponse
 *    before handing it to the sender and the AI. Calling it again caused
 *    inventory/vision to be processed twice (double-decrement on prend, etc.)
 *
 * 4. prend/pose matching key is just "prend"/"pose"
 *    The server echoes back cmd+arg, and CommandSender already does the
 *    "prend resource" compound-key matching. The AI just needs to register
 *    the compound key consistently to match what CommandSender expects.
 *
 * 5. Stone-dropping before incantation is removed
 *    The server only cares that the stones are on the tile, not who owns them.
 *    Dropping to the floor then immediately incantating is correct; the previous
 *    logic was checking "stonesOnTile - needed" which could go negative.
 *
 * 6. Incantation participant path
 *    When we receive an incantation_start event we are a PARTICIPANT —
 *    we must NOT send another incantation command, just freeze and wait for
 *    a level-up or a timeout.
 *
 * 7. Vision staleness gate
 *    decideNextAction refuses to act if the last vision update is older than
 *    VISION_STALE_MS. This avoids decisions based on data that is several
 *    seconds out of date.
 */

#include "SimpleAI.hpp"
#include "helpers/Logger.hpp"

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <unistd.h>

namespace zappy {

// --------------------------------------------------------------------------
// helpers
// --------------------------------------------------------------------------

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

static void clearQueue(std::queue<NavigationStep>& q) {
    std::queue<NavigationStep> empty;
    std::swap(q, empty);
}

// --------------------------------------------------------------------------
// construction
// --------------------------------------------------------------------------

SimpleAI::SimpleAI(WorldState& state, CommandSender& sender)
    : _state(state), _sender(sender)
{
    const char* easyAsc = std::getenv("ZAPPY_EASY_ASCENSION");
    _easyAscensionMode = (easyAsc != nullptr && std::string(easyAsc) == "1");
    _clientTag = static_cast<int>((nowMs() ^ static_cast<int64_t>(getpid())) % 1000000);
    if (_clientTag < 0)
        _clientTag = -_clientTag;
    Logger::info("SimpleAI created, easyAscension=" + std::string(_easyAscensionMode ? "yes" : "no"));
}

// --------------------------------------------------------------------------
// tick — called from network thread ~50 ms cadence
// --------------------------------------------------------------------------

void SimpleAI::tick(int64_t now) {
    // 1. housekeeping: timeout detection (does NOT cancel; CommandSender does)
    _sender.checkTimeouts(_commandTimeoutMs);

    // 2. unblock if our tracked command timed out at the sender level
    if (_waitingForCmd && (now - _cmdSentAt > _commandTimeoutMs + 2000)) {
        Logger::warn("AI: hard-timeout on command, resetting to Idle");
        _waitingForCmd = false;
        _AIstate       = AIState::Idle;
        clearQueue(_actionQueue);
    }

    // 3. periodic sensor refreshes — fire-and-forget, no expectResponse
    if (now - _lastVoirTime > VOIR_INTERVAL_MS && !_waitingForCmd) {
        _lastVoirTime = now;
        _sender.sendVoir();
        Logger::debug("AI: sent voir (fire-and-forget)");
    }
    if (now - _lastInventaireTime > INVENTAIRE_INTERVAL_MS && !_waitingForCmd) {
        _lastInventaireTime = now;
        _sender.sendInventaire();
        Logger::debug("AI: sent inventaire (fire-and-forget)");
    }

    // 4. wait if we are blocked on a tracked command
    if (_waitingForCmd) {
        Logger::debug("AI: waiting for tracked command response");
        return;
    }

    // 5. execute queued actions first
    if (!_actionQueue.empty()) {
        if (_state.getFood() < _foodEmergencyThreshold && _currentResourceTarget != "nourriture") {
            Logger::warn("AI: emergency food preemption, dropping current plan");
            clearQueue(_actionQueue);
            startGathering("nourriture", now);
            return;
        }
        executeNextAction(now);
        return;
    }

    // 6. high-level decision
    decideNextAction(now);
}

// --------------------------------------------------------------------------
// decideNextAction
// --------------------------------------------------------------------------

void SimpleAI::decideNextAction(int64_t now) {
    // --- guard: currently participating in someone else's incantation ---
    if (_AIstate == AIState::Incantating) {
        if (now - _lastIncantationTime > INCANTATION_TIMEOUT_MS) {
            Logger::warn("AI: incantation participation timed out, resetting");
            _AIstate = AIState::Idle;
        } else {
            Logger::debug("AI: waiting for incantation to finish");
        }
        return;
    }

    // --- guard: need fresh vision data before acting ---
    if (_state.getVision().empty()) {
        Logger::debug("AI: no vision data yet, waiting");
        return;
    }
    if (now - _state.getLastVisionTime() > VISION_STALE_MS) {
        Logger::debug("AI: vision data stale, waiting for refresh");
        return;
    }

    // ================================================================
    // Priority 1: survival — food
    // ================================================================
    if (shouldGetFood()) {
        Logger::info("AI: food=" + std::to_string(_state.getFood()) + " → gathering nourriture");
        startGathering("nourriture", now);
        return;
    }

    // ================================================================
    // Priority 2: incantation — level up
    // ================================================================
    if (shouldIncantate(now)) {
        Logger::info("AI: ready to incantate for level " + std::to_string(_state.getLevel() + 1));
        // Drop the exact required stones onto the tile first (if not already there)
        if (!_easyAscensionMode) {
            auto drops = buildStoneDropPlan();
            if (!drops.empty()) {
                Logger::info("AI: dropping " + std::to_string(drops.size()) + " stone(s) before incantation");
                for (const auto& step : drops)
                    _actionQueue.push(step);
                // After drops complete, onCommandComplete re-enters decideNextAction
                // which will then call startIncantation
                executeNextAction(now);
                return;
            }
        }
        startIncantation(now);
        return;
    }

    // ================================================================
    // Priority 3: wait + broadcast if stones ready but no players
    // ================================================================
    if (!_easyAscensionMode && _state.hasStonesForIncantation() && !_state.hasEnoughPlayers()) {
        // Drop stones onto the tile so arriving players see them
        auto drops = buildStoneDropPlan();
        if (!drops.empty()) {
            Logger::info("AI: placing stones on tile while waiting for players");
            for (const auto& step : drops)
                _actionQueue.push(step);
            executeNextAction(now);
            return;
        }
        // Broadcast our position periodically
        if (now - _lastBroadcastTime > BROADCAST_INTERVAL_MS && shouldLeadIncantation(now)) {
            _lastBroadcastTime = now;
            Logger::info("AI: broadcasting INCANT_READY lv" + std::to_string(_state.getLevel()) +
                " anchor=" + std::to_string(_clientTag));
            _sender.sendBroadcast("INCANT_READY " + std::to_string(_state.getLevel()) +
                " " + std::to_string(_clientTag));
        }
        return; // hold position
    }

    // ================================================================
    // Priority 4: gather missing stones
    // ================================================================
    {
        auto missing = _state.getMissingStones();
        if (!missing.empty()) {
            Logger::info("AI: need stone '" + missing[0] + "'");
            startGathering(missing[0], now);
            return;
        }
    }

    // ================================================================
    // Priority 5: fork
    // ================================================================
    if (shouldFork(now)) {
        Logger::info("AI: forking");
        startFork(now);
        return;
    }

    // ================================================================
    // Priority 6: explore
    // ================================================================
    Logger::debug("AI: exploring");
    _AIstate = AIState::Exploring;
    auto plan = _planner.planExploration(_state);
    for (const auto& step : plan)
        _actionQueue.push(step);
    if (!_actionQueue.empty())
        executeNextAction(now);
}

// --------------------------------------------------------------------------
// shouldGetFood
// --------------------------------------------------------------------------

bool SimpleAI::shouldGetFood() const {
    int food = _state.getFood();
    if (food < _foodEmergencyThreshold) return true;
    if (food < _foodComfortThreshold)   return true;

    return false;
}

// --------------------------------------------------------------------------
// shouldFork
// --------------------------------------------------------------------------

bool SimpleAI::shouldFork(int64_t now) const {
    if (!_forkEnabled)                       return false;
    if (_forkCount >= _maxForks)             return false;
    if (_state.getFood() < _forkFoodThreshold) return false;
    if (now - _lastForkTime < FORK_COOLDOWN_MS) return false;
    return true;
}

// --------------------------------------------------------------------------
// shouldIncantate
// --------------------------------------------------------------------------

bool SimpleAI::shouldIncantate(int64_t now) const {
    if (_state.getLevel() >= _targetLevel)          return false;
    if (now - _lastIncantationTime < INCANT_COOLDOWN_MS) return false;
    if (_easyAscensionMode)                         return true;
    return _state.canIncantate();
}

// --------------------------------------------------------------------------
// buildStoneDropPlan
// Returns a list of Place steps for stones we carry that aren't yet on tile.
// --------------------------------------------------------------------------

std::vector<NavigationStep> SimpleAI::buildStoneDropPlan() const {
    std::vector<NavigationStep> drops;
    if (_state.getLevel() >= 8) return drops;

    auto req = _state.getLevelRequirement(_state.getLevel());
    const auto& vision = _state.getVision();

    // Count what's already on the tile
    std::map<std::string, int> onTile;
    if (!vision.empty()) {
        for (const auto& item : vision[0].items)
            onTile[item]++;
    }

    const auto& inv = _state.getInventory();
    for (const auto& [stone, needed] : req.stonesNeeded) {
        int alreadyThere = onTile.count(stone) ? onTile.at(stone) : 0;
        int stillNeeded  = needed - alreadyThere;
        if (stillNeeded <= 0) continue;

        auto it = inv.find(stone);
        int have = (it != inv.end()) ? it->second : 0;
        int toDrop = std::min(stillNeeded, have);
        for (int i = 0; i < toDrop; i++)
            drops.push_back({NavAction::Place, stone});
    }
    return drops;
}

// --------------------------------------------------------------------------
// startGathering
// --------------------------------------------------------------------------

void SimpleAI::startGathering(const std::string& resource, int64_t now) {
    _AIstate              = AIState::Gathering;
    _currentResourceTarget = resource;

    // Resource on current tile → take immediately
    const auto& vision = _state.getVision();
    if (!vision.empty() && vision[0].hasItem(resource)) {
        Logger::info("AI: " + resource + " on current tile, taking now");
        issueTrackedCommand(
            "prend " + resource,
            [this](const ServerMessage& msg) { onCommandComplete(msg); },
            now
        );
        _sender.sendPrend(resource);
        return;
    }

    // Plan a path
    auto plan = _planner.planPathToResource(_state, resource);
    if (plan.empty()) {
        Logger::warn("AI: no path to " + resource + ", falling back to explore");
        _AIstate = AIState::Exploring;
        plan = _planner.planExploration(_state);
        if (plan.empty()) {
            Logger::error("AI: exploration plan also empty, going idle");
            _AIstate = AIState::Idle;
            return;
        }
    }

    Logger::info("AI: gathering " + resource + " with " + std::to_string(plan.size()) + " steps");
    for (const auto& step : plan)
        _actionQueue.push(step);
    executeNextAction(now);
}

// --------------------------------------------------------------------------
// startIncantation
// --------------------------------------------------------------------------

void SimpleAI::startIncantation(int64_t now) {
    _AIstate            = AIState::Incantating;
    _lastIncantationTime = now;

    Logger::info("AI: sending incantation");
    issueTrackedCommand(
        "incantation",
        [this](const ServerMessage& msg) { onCommandComplete(msg); },
        now
    );
    _sender.sendIncantation();
}

// --------------------------------------------------------------------------
// startFork
// --------------------------------------------------------------------------

void SimpleAI::startFork(int64_t now) {
    _AIstate      = AIState::WaitingForResponse;
    _lastForkTime = now;
    _forkCount++;

    Logger::info("AI: sending fork");
    issueTrackedCommand(
        "fork",
        [this](const ServerMessage& msg) { onCommandComplete(msg); },
        now
    );
    _sender.sendFork();
}

bool SimpleAI::shouldLeadIncantation(int64_t now) const {
    if (_state.getFood() < _foodComfortThreshold)
        return false;

    if (_selectedAnchorTag != -1 && _selectedAnchorTag != _clientTag && now < _anchorStickyUntil)
        return false;

    return true;
}

// --------------------------------------------------------------------------
// issueTrackedCommand — registers expectResponse and sets _waitingForCmd
// --------------------------------------------------------------------------

void SimpleAI::issueTrackedCommand(
    const std::string& key,
    std::function<void(const ServerMessage&)> cb,
    int64_t now)
{
    _pendingCommandId = _sender.expectResponse(key, cb);
    _waitingForCmd    = true;
    _cmdSentAt        = now;
    _lastCommandTime  = now;
}

// --------------------------------------------------------------------------
// executeNextAction
// --------------------------------------------------------------------------

void SimpleAI::executeNextAction(int64_t now) {
    if (_actionQueue.empty()) {
        Logger::debug("AI: action queue empty → Idle");
        _AIstate = AIState::Idle;
        return;
    }

    auto step = _actionQueue.front();
    _actionQueue.pop();

    Logger::info("AI: execute " + step.toString() +
                 " (queue remaining=" + std::to_string(_actionQueue.size()) + ")");

    switch (step.action) {
        case NavAction::MoveForward:
            issueTrackedCommand("avance",
                [this](const ServerMessage& msg) { onCommandComplete(msg); }, now);
            _sender.sendAvance();
            break;

        case NavAction::TurnLeft:
            issueTrackedCommand("gauche",
                [this](const ServerMessage& msg) { onCommandComplete(msg); }, now);
            _sender.sendGauche();
            break;

        case NavAction::TurnRight:
            issueTrackedCommand("droite",
                [this](const ServerMessage& msg) { onCommandComplete(msg); }, now);
            _sender.sendDroite();
            break;

        case NavAction::Take:
            issueTrackedCommand("prend " + step.resource,
                [this, step](const ServerMessage& msg) {
                    if (msg.isOk())
                        Logger::info("AI: took " + step.resource);
                    else
                        Logger::warn("AI: failed to take " + step.resource);
                    onCommandComplete(msg);
                }, now);
            _sender.sendPrend(step.resource);
            break;

        case NavAction::Place:
            issueTrackedCommand("pose " + step.resource,
                [this, step](const ServerMessage& msg) {
                    if (msg.isOk())
                        Logger::info("AI: placed " + step.resource);
                    else
                        Logger::warn("AI: failed to place " + step.resource);
                    onCommandComplete(msg);
                }, now);
            _sender.sendPose(step.resource);
            break;

        default:
            Logger::error("AI: unknown NavAction!");
            _waitingForCmd = false;
            break;
    }
}

// --------------------------------------------------------------------------
// onCommandComplete — called by the expectResponse callback
// NOTE: WorldState has ALREADY been updated by Client::processIncomingMessages
//       before the sender dispatched this callback. Do NOT call onResponse again.
// --------------------------------------------------------------------------

void SimpleAI::onCommandComplete(const ServerMessage& msg) {
    _waitingForCmd = false;

    // --- timeout ---
    if (msg.status == "timeout") {
        Logger::warn("AI: command timeout for '" + msg.cmd + "', clearing queue");
        clearQueue(_actionQueue);
        _AIstate = AIState::Idle;
        return;
    }

    // --- incantation is special: "ko" can mean "we're a participant, not initiator" ---
    if (msg.cmd == "incantation") {
        if (msg.isOk()) {
            Logger::info("AI: incantation succeeded → level up expected");
            // Stay Incantating; the level-up event will reset us
        } else if (msg.isKo()) {
            Logger::warn("AI: incantation ko — not enough resources/players");
            _AIstate = AIState::Idle;
        }
        // either way we don't chain into next action here; wait for the event
        return;
    }

    // --- generic failure ---
    if (!msg.isOk()) {
        Logger::warn("AI: command '" + msg.cmd + "' failed (" + msg.status + "), clearing queue");
        clearQueue(_actionQueue);
        _AIstate = AIState::Idle;
        return;
    }

    Logger::info("AI: command '" + msg.cmd + "' ok");

    // Continue queued actions if any
    if (!_actionQueue.empty()) {
        executeNextAction(nowMs());
    } else {
        _AIstate = AIState::Idle;
    }
}

// --------------------------------------------------------------------------
// onMessage — called for events AND broadcasts
// --------------------------------------------------------------------------

void SimpleAI::onMessage(const ServerMessage& msg) {
    // ---- server events ----
    if (msg.type == ServerMessageType::Event) {
        // incantation_start: we are a participant — freeze
        bool isIncantStart =
            (msg.eventType.has_value() && *msg.eventType == "incantation_start") ||
            msg.status == "incantation_start";

        if (isIncantStart && _AIstate != AIState::Incantating) {
            Logger::info("AI: incantation_start event → locking as participant");
            clearQueue(_actionQueue);
            _waitingForCmd = false;        // release any tracked command
            _AIstate             = AIState::Incantating;
            _lastIncantationTime = nowMs();
            _selectedAnchorTag = -1;
            _anchorStickyUntil = 0;
            return;
        }

        // level-up: unlock from incantating state
        if (msg.isLevelUp()) {
            Logger::info("AI: level-up event → resetting to Idle (was " +
                         std::to_string(static_cast<int>(_AIstate)) + ")");
            _AIstate = AIState::Idle;
            _lastIncantationTime = 0; // reset cooldown so we can act immediately
            _selectedAnchorTag = -1;
            _anchorStickyUntil = 0;
            return;
        }
        return;
    }

    // ---- broadcast messages for coordination ----
    if (msg.type == ServerMessageType::Message && msg.messageText.has_value()) {
        const std::string& text = *msg.messageText;

        if (text.find("INCANT_READY") != std::string::npos) {
            std::istringstream ss(text);
            std::string prefix;
            int level = 0;
            int anchorTag = -1;
            ss >> prefix >> level;
            if (!(ss >> anchorTag))
                anchorTag = -1;

            // Ignore if different level
            if (level != _state.getLevel()) return;

            // Ignore if we're already incantating or don't have a direction
            if (_AIstate == AIState::Incantating || !msg.direction.has_value()) return;

            int64_t now = nowMs();
            if (_anchorStickyUntil > 0 && now >= _anchorStickyUntil) {
                _selectedAnchorTag = -1;
                _anchorStickyUntil = 0;
            }

            if (anchorTag != -1) {
                if (_selectedAnchorTag == -1 || anchorTag == _selectedAnchorTag || anchorTag < _selectedAnchorTag) {
                    _selectedAnchorTag = anchorTag;
                    _anchorStickyUntil = now + 15000;
                }
                if (_selectedAnchorTag != anchorTag)
                    return;
            }

            if (now - _lastAnchorFollowTime < 600)
                return;

            if (_state.getFood() < _foodComfortThreshold)
                return;

            _lastAnchorFollowTime = now;

            int dir = *msg.direction;
            Logger::info("AI: INCANT_READY lv" + std::to_string(level) + " from dir " + std::to_string(dir));

            _lastBroadcastTime = now;

            if (dir == 0) {
                // Already on the right tile — stop and wait
                Logger::info("AI: on target tile, stopping to join incantation");
                clearQueue(_actionQueue);
                _waitingForCmd = false;
                _AIstate       = AIState::Idle;
                _targetBroadcastDir = 0;
            } else {
                // Move one step towards the broadcaster
                _targetBroadcastDir = dir;
                clearQueue(_actionQueue);
                _waitingForCmd = false;

                auto plan = _planner.planApproachDirection(_state, dir);
                for (const auto& step : plan)
                    _actionQueue.push(step);

                Logger::info("AI: moving towards INCANT_READY source (dir=" + std::to_string(dir) + ")");
                if (!_actionQueue.empty())
                    executeNextAction(nowMs());
            }
        }
    }
}

} // namespace zappy
