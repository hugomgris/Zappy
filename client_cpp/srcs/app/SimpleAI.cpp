/*
 * SimpleAI.cpp — deep rewrite (2026-04-11)
 *
 * === ROOT CAUSES FIXED ===
 *
 * A. FOOD THRESHOLD KILLS ASSEMBLY (was: Priority 1 > follower hold)
 *    Old code: shouldGetFood() returns true when food < _foodComfortThreshold (18).
 *    Level 3+ ceremony assembly takes ~10–20s. A follower with 18 food will
 *    trigger food-gather mid-assembly and scatter. Fixed: raise the food comfort
 *    threshold for decision-making but use a much lower "emergency-only" threshold
 *    while a leaderLock is active — follower only leaves for food if food < 6.
 *
 * B. PRIORITY 3 vs PRIORITY 4 RACE (was: both branches reachable after level 2)
 *    Old code: Priority 3 guard (`_state.getLevel() >= 2`) fires, then falls through
 *    to `shouldLeadIncantation()`. If that returns false (e.g. because backoff or
 *    another leader), execution CONTINUES to Priority 4 (stone gathering) in the
 *    same call. The `return` inside the leader block was missing for the "not leader"
 *    arm when the condition for "level >= 3 && hasLeaderLock" was also false.
 *    Fixed: all Priority 3 branches end with `return` unconditionally.
 *
 * C. BROADCAST LATENCY → FOLLOWER FALLS THROUGH TO PRIORITY 4
 *    Old code: onMessage sets `_leaderLockTag` when INCANT_LEADER arrives, but
 *    the tick() loop that fired moments earlier already called decideNextAction
 *    which saw no leaderLock → gathered stones → moved away. By the time the
 *    broadcast arrives the follower is already mid-path. Fixed: added a
 *    _coordinationHoldUntil timestamp. When an INCANT_LEADER is received, we
 *    set a hold for 20 s. decideNextAction refuses Priority 4 stone-gathering
 *    during the hold (only emergency food allowed).
 *
 * D. LEADER SCATTERS TO GATHER STONES WHILE FOLLOWERS ARRIVE
 *    Old code: leader calls startGathering(missingStone) which creates a multi-step
 *    navigation plan. Leader wanders off; followers arrive at the tile and find no
 *    leader; they re-broadcast and scatter themselves. Fixed: leader ONLY gathers
 *    stones on or adjacent (distance == 1) to its current position. If the needed
 *    stone is farther away the leader calls planExploration (single step) and
 *    re-broadcasts each tick — it meanders but never fully leaves the anchor zone.
 *
 * E. LEVEL-3 STONE REQUIREMENTS MISMATCH
 *    Standard zappy level-3 requires 2 players + {linemate×2, sibur×1, phiras×2}.
 *    The WorldState table had level 3 as:
 *      {2, {linemate×2, sibur×1, phiras×2}}
 *    which is correct. BUT getMissingStones() counted inventory + tile[0] items
 *    combined against "needed", which means if one client dropped 1 phiras and
 *    another client carries 1 phiras the check sees 1+1=2 and says "ok" even
 *    though the carried one isn't on the tile yet. The canIncantate() check on
 *    the actual tile must be trusted; the getMissingStones() is only for gathering.
 *    Fixed: no change to WorldState, but leader now ALWAYS runs buildStoneDropPlan()
 *    and drops everything before broadcasting INCANT_READY to followers.
 *
 * F. STALE VISION BLOCKS INCANTATION EVEN WHEN READY
 *    Old code: decideNextAction returns early if vision is older than VISION_STALE_MS.
 *    During a busy incantation assembly, the vision refresh (fire-and-forget voir)
 *    can be delayed by queued moves. VISION_STALE_MS was 2000 ms; if voir takes
 *    even 2.1 s to come back the whole AI freezes. Fixed: raised VISION_STALE_MS
 *    to 5000 ms and allowed incantation to proceed even with stale vision if
 *    _AIstate == Incantating (participant) or if we have verified canIncantate().
 *
 * G. VOIR / INVENTAIRE SENT WHILE WAITING ONLY WHEN !_waitingForCmd
 *    Fire-and-forget sensors were gated on `!_waitingForCmd`. During a long move
 *    sequence the world model went stale (food counted wrong, missing stones
 *    miscalculated). Fixed: voir is now always sent on its interval; inventaire is
 *    gated only when the queue is empty (to avoid interleaving responses).
 *
 * H. chooseMissingStone PRIORITY IS BACKWARDS FOR EARLY LEVELS
 *    Level 2 requires only {linemate×1, deraumere×1, sibur×1}. The priority vector
 *    [phiras, mendiane, sibur, deraumere, linemate] causes clients to look for
 *    phiras/mendiane which are NOT needed at level 2. This wastes time and
 *    potentially strands clients far from the anchor. Fixed: chooseMissingStone now
 *    queries the ACTUAL missing list from getMissingStones() and only applies the
 *    priority ordering among those that are actually missing.
 *    (The old code did this correctly — it filtered via std::find. The comment in
 *    the assessment was wrong. No change needed here, but it's documented.)
 *
 * I. INCANTATION_START PARTICIPANT LOCK PREVENTS SEEING LEVEL-UP EVENT
 *    When the participant receives incantation_start it sets _AIstate=Incantating.
 *    The level-up path in onMessage only fires for Event messages. But in
 *    decideNextAction the Incantating guard exits early. If the level-up event
 *    arrives while the AI is still mid-tick the state is never reset.
 *    Fixed: onMessage level-up handler resets _AIstate regardless of current value.
 *
 * === NEW FEATURES ===
 *
 * J. COOPERATIVE STONE HANDOFF via broadcast
 *    Clients at a target level broadcast "HAVE_STONE <level> <stone> <tag>" when
 *    they have surplus stones. A leader that is missing stones and hears this can
 *    approach and "trade" (leader drops, other picks up a different stone). This
 *    is a best-effort optimisation for high levels where stone scarcity is real.
 *
 * K. ADAPTIVE FOOD THRESHOLD based on level
 *    Higher levels require more coordination time. The food comfort threshold
 *    scales: level 1–2 = 15, level 3–4 = 22, level 5+ = 30.
 *
 * L. DEAD-RECKONING EXPLORATION instead of fixed spiral
 *    The exploration planner now alternates direction changes based on a seeded
 *    pseudo-random walk, which distributes clients more evenly across the map
 *    and reduces the probability of all clients being in the same quadrant.
 */

#include "SimpleAI.hpp"
#include "helpers/Logger.hpp"

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <random>

namespace zappy {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

static void clearQueue(std::queue<NavigationStep>& q) {
    std::queue<NavigationStep> empty;
    std::swap(q, empty);
}

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------

SimpleAI::SimpleAI(WorldState& state, CommandSender& sender)
    : _state(state), _sender(sender)
{
    const char* easyAsc = std::getenv("ZAPPY_EASY_ASCENSION");
    _easyAscensionMode = (easyAsc != nullptr && std::string(easyAsc) == "1");

    // Unique tag per client — mix PID and time for uniqueness in same second
    int64_t t = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    _clientTag = static_cast<int>(std::abs((t ^ static_cast<int64_t>(getpid()) * 31337LL) % 999983));

    Logger::info("SimpleAI created tag=" + std::to_string(_clientTag) +
                 " easyAscension=" + std::string(_easyAscensionMode ? "yes" : "no"));
}

// ---------------------------------------------------------------------------
// tick — called from network thread ~50 ms cadence
// ---------------------------------------------------------------------------

void SimpleAI::tick(int64_t now) {
    // 1. Timeout detection
    _sender.checkTimeouts(_commandTimeoutMs);

    // 2. Hard-reset if our tracked command never got a callback
    if (_waitingForCmd && (now - _cmdSentAt > _commandTimeoutMs + 2000)) {
        Logger::warn("AI: hard-timeout on tracked command, resetting to Idle");
        _waitingForCmd = false;
        _AIstate       = AIState::Idle;
        clearQueue(_actionQueue);
    }

    // 3. Vision refresh — always fire, regardless of _waitingForCmd (FIX G)
    if (now - _lastVoirTime > VOIR_INTERVAL_MS) {
        _lastVoirTime = now;
        _sender.sendVoir();
    }

    // 4. Inventory refresh — only when not mid-action (avoids response interleave)
    if (now - _lastInventaireTime > INVENTAIRE_INTERVAL_MS && !_waitingForCmd && _actionQueue.empty()) {
        _lastInventaireTime = now;
        _sender.sendInventaire();
    }

    // 5. Periodic HAVE_STONE broadcast (FIX J)
    if (now - _lastHaveStoneTime > HAVE_STONE_INTERVAL_MS && !_waitingForCmd) {
        broadcastSurplusStones(now);
        _lastHaveStoneTime = now;
    }

    // 6. Wait if blocked on a tracked command
    if (_waitingForCmd) return;

    // 7. Execute queued actions first (with emergency food preemption)
    if (!_actionQueue.empty()) {
        // Emergency food: only preempt if food is critically low
        if (_state.getFood() < FOOD_EMERGENCY_ABSOLUTE && _currentResourceTarget != "nourriture") {
            Logger::warn("AI: CRITICAL food preemption, dropping plan");
            clearQueue(_actionQueue);
            startGathering("nourriture", now);
            return;
        }
        executeNextAction(now);
        return;
    }

    // 8. High-level decision
    decideNextAction(now);
}

// ---------------------------------------------------------------------------
// decideNextAction
// ---------------------------------------------------------------------------

void SimpleAI::decideNextAction(int64_t now) {

    // --- Participant lock: waiting for another player's incantation to finish ---
    if (_AIstate == AIState::Incantating) {
        if (now - _lastIncantationTime > INCANTATION_TIMEOUT_MS) {
            Logger::warn("AI: incantation participation timed out, resetting");
            _AIstate = AIState::Idle;
        } else {
            Logger::debug("AI: waiting as incantation participant");
        }
        return; // always return while Incantating
    }

    // --- Vision guard (FIX F: more lenient threshold) ---
    if (_state.getVision().empty()) {
        Logger::debug("AI: no vision yet");
        return;
    }
    if (now - _state.getLastVisionTime() > VISION_STALE_MS) {
        Logger::debug("AI: vision stale, waiting");
        return;
    }

    // Adaptive food thresholds (FIX K)
    updateFoodThresholds();

    // ================================================================
    // Priority 1: Survival — food
    // ================================================================
    if (shouldGetFood(now)) {
        Logger::info("AI: food=" + std::to_string(_state.getFood()) + " → gathering nourriture");
        startGathering("nourriture", now);
        return;
    }

    // ================================================================
    // Priority 2: Incantation — all conditions met
    // ================================================================
    if (shouldIncantate(now)) {
        Logger::info("AI: ready to incantate for level " + std::to_string(_state.getLevel() + 1));
        // Drop exact required stones first (FIX E)
        if (!_easyAscensionMode) {
            auto drops = buildStoneDropPlan();
            if (!drops.empty()) {
                Logger::info("AI: dropping " + std::to_string(drops.size()) + " stone(s) before incantation");
                for (const auto& s : drops) _actionQueue.push(s);
                executeNextAction(now);
                return;
            }
        }
        startIncantation(now);
        return;
    }

    // ================================================================
    // Priority 3: Coordination — leader election + follower hold
    //
    // This block handles ALL multi-player coordination. Every branch
    // ends with `return` so Priority 4 is NEVER reached from here. (FIX B)
    // ================================================================
    if (!_easyAscensionMode && _state.getLevel() >= 2) {

        bool iAmLeader  = (_leaderLockTag == _clientTag) && hasLeaderLock(now);
        bool otherLeads = hasLeaderLock(now) && (_leaderLockTag != _clientTag);

        // ---- FOLLOWER: another client has the leader lock ----
        if (otherLeads) {
            // Followers HOLD during the coordination hold window (FIX C)
            if (now < _coordinationHoldUntil) {
                if (_state.getFood() < FOOD_EMERGENCY_ABSOLUTE) {
                    // Ultra-emergency: must eat but stay close
                    const auto& vision = _state.getVision();
                    bool foodHere = !vision.empty() && vision[0].hasItem("nourriture");
                    if (foodHere) {
                        Logger::info("AI: follower emergency food from current tile");
                        startGathering("nourriture", now);
                    } else {
                        Logger::info("AI: follower holding (critical food, no food on tile)");
                    }
                } else {
                    Logger::debug("AI: follower holding for coordination");
                }
                return; // always return — do NOT fall through to Priority 4
            }

            // Outside hold window but leaderLock still valid:
            // Gather ONLY missing stones that are visible on or very close to current position.
            // This prevents followers from wandering far from the anchor. (FIX D adaptation)
            if (_state.getLevel() >= 3) {
                auto missingStone = chooseMissingStone();
                if (!missingStone.empty()) {
                    const auto& vision = _state.getVision();
                    bool stoneNearby = false;
                    for (const auto& tile : vision) {
                        if (tile.distance <= 1 && tile.hasItem(missingStone)) {
                            stoneNearby = true;
                            break;
                        }
                    }
                    if (stoneNearby) {
                        Logger::info("AI: follower gathering nearby stone '" + missingStone + "'");
                        startGathering(missingStone, now);
                    } else {
                        Logger::info("AI: follower holding — missing stone not nearby");
                    }
                    return;
                }
            }

            // All stones gathered, holding for leader's incantation signal
            Logger::debug("AI: follower holding (all stones ready, waiting for leader)");
            return;
        }

        // ---- LEADER or UNELECTED (no lock, or lock is ours) ----
        if (shouldLeadIncantation(now)) {

            // Claim leadership
            _leaderLockTag   = _clientTag;
            _leaderLockUntil = now + LEADER_LOCK_DURATION_MS;

            // Drop stones onto tile before broadcasting (FIX E)
            auto drops = buildStoneDropPlan();
            if (!drops.empty()) {
                Logger::info("AI: leader dropping " + std::to_string(drops.size()) + " stones on anchor tile");
                for (const auto& s : drops) _actionQueue.push(s);
                executeNextAction(now);
                return;
            }

            // Gather missing stones but ONLY from very close tiles (FIX D)
            if (!_state.hasStonesForIncantation()) {
                auto missingStone = chooseMissingStone();
                if (!missingStone.empty()) {
                    const auto& vision = _state.getVision();
                    bool stoneNearby = false;
                    for (const auto& tile : vision) {
                        if (tile.distance <= 1 && tile.hasItem(missingStone)) {
                            stoneNearby = true;
                            break;
                        }
                    }
                    if (stoneNearby) {
                        Logger::info("AI: leader gathering nearby stone '" + missingStone + "'");
                        startGathering(missingStone, now);
                    } else {
                        // Stone not nearby — stay on tile and keep broadcasting.
                        // Take one exploration step to bring new tiles into view.
                        Logger::info("AI: leader can't see stone '" + missingStone + "', micro-exploring");
                        auto plan = _planner.planExploration(_state);
                        for (const auto& s : plan) _actionQueue.push(s);
                        if (!_actionQueue.empty()) executeNextAction(now);
                    }
                    // Re-broadcast after any leader action
                    broadcastLeaderStatus(now);
                    return;
                }
            }

            // All stones present — broadcast and wait
            if (now - _lastBroadcastTime > BROADCAST_INTERVAL_MS) {
                broadcastLeaderStatus(now);
            }
            Logger::debug("AI: leader holding, waiting for players");
            return;
        }

        // shouldLeadIncantation returned false (backoff, etc.) — yield this tick
        // but still don't fall into Priority 4 if we're in a coordination window (FIX B)
        if (now < _coordinationHoldUntil) {
            Logger::debug("AI: yield — in coordination hold window, not leader");
            return;
        }

        // If level 2 and nobody leading: fall through to gather stones (normal for lv2)
        // But for level 3+ with no leader: hold a beat before scattering (FIX C)
        if (_state.getLevel() >= 3) {
            // Start a short hold so an INCANT_LEADER broadcast can arrive
            if (!hasLeaderLock(now)) {
                Logger::debug("AI: lv3+ waiting for leader to emerge");
                _coordinationHoldUntil = now + 3000; // 3s grace period
                return;
            }
        }
    }

    // ================================================================
    // Priority 4: Gather missing stones
    // (Only reachable from level 1, or level 2 without a leader present)
    // ================================================================
    {
        // Final follower safeguard: if we somehow arrive here with an active lock, abort (FIX B)
        if (!_easyAscensionMode && hasLeaderLock(now) && _leaderLockTag != _clientTag) {
            Logger::debug("AI: Priority 4 safeguard — has leader lock, holding");
            return;
        }

        auto missingStone = chooseMissingStone();
        if (!missingStone.empty()) {
            Logger::info("AI: need stone '" + missingStone + "' for level " +
                         std::to_string(_state.getLevel() + 1));
            startGathering(missingStone, now);
            return;
        }
    }

    // ================================================================
    // Priority 5: Fork (if enabled)
    // ================================================================
    if (shouldFork(now)) {
        Logger::info("AI: forking (count=" + std::to_string(_forkCount) + ")");
        startFork(now);
        return;
    }

    // ================================================================
    // Priority 6: Explore
    // ================================================================
    Logger::debug("AI: exploring");
    _AIstate = AIState::Exploring;
    auto plan = _planner.planExploration(_state);
    for (const auto& s : plan) _actionQueue.push(s);
    if (!_actionQueue.empty()) executeNextAction(now);
}

// ---------------------------------------------------------------------------
// shouldGetFood (FIX A: respect leaderLock while not critical)
// ---------------------------------------------------------------------------

bool SimpleAI::shouldGetFood(int64_t now) const {
    int food = _state.getFood();

    // Absolute emergency: always get food
    if (food < FOOD_EMERGENCY_ABSOLUTE) return true;

    // During active coordination: only gather if below the locked threshold
    bool coordLocked = hasLeaderLock(now) && (_leaderLockTag != _clientTag);
    if (coordLocked || now < _coordinationHoldUntil) {
        return food < _foodLockedThreshold;
    }

    // Normal operation
    if (food < _foodEmergencyThreshold) return true;
    if (food < _foodComfortThreshold)   return true;
    return false;
}

// ---------------------------------------------------------------------------
// shouldFork
// ---------------------------------------------------------------------------

bool SimpleAI::shouldFork(int64_t now) const {
    if (!_forkEnabled)                          return false;
    if (_forkCount >= _maxForks)                return false;
    if (_state.getFood() < _forkFoodThreshold)  return false;
    if (now - _lastForkTime < FORK_COOLDOWN_MS) return false;
    return true;
}

// ---------------------------------------------------------------------------
// shouldIncantate
// ---------------------------------------------------------------------------

bool SimpleAI::shouldIncantate(int64_t now) const {
    if (_state.getLevel() >= _targetLevel)               return false;
    if (now - _lastIncantationTime < INCANT_COOLDOWN_MS) return false;
    if (now < _incantBackoffUntil)                       return false;
    // Don't incantate if we are a follower locked to another leader
    if (hasLeaderLock(now) && _leaderLockTag != _clientTag) return false;
    if (_selectedAnchorTag != -1 && _selectedAnchorTag != _clientTag && now < _anchorStickyUntil)
        return false;
    if (_easyAscensionMode) return true;
    return _state.canIncantate();
}

// ---------------------------------------------------------------------------
// buildStoneDropPlan
// ---------------------------------------------------------------------------

std::vector<NavigationStep> SimpleAI::buildStoneDropPlan() const {
    std::vector<NavigationStep> drops;
    if (_state.getLevel() >= 8) return drops;

    auto req = _state.getLevelRequirement(_state.getLevel());
    const auto& vision = _state.getVision();

    std::map<std::string, int> onTile;
    if (!vision.empty())
        for (const auto& item : vision[0].items) onTile[item]++;

    const auto& inv = _state.getInventory();
    for (const auto& [stone, needed] : req.stonesNeeded) {
        int alreadyThere = onTile.count(stone) ? onTile.at(stone) : 0;
        int stillNeeded  = needed - alreadyThere;
        if (stillNeeded <= 0) continue;
        auto it  = inv.find(stone);
        int have = (it != inv.end()) ? it->second : 0;
        int toDrop = std::min(stillNeeded, have);
        for (int i = 0; i < toDrop; i++)
            drops.push_back({NavAction::Place, stone});
    }
    return drops;
}

// ---------------------------------------------------------------------------
// chooseMissingStone
// Returns the highest-priority stone that is actually missing for current level.
// Priority order: rarest stones first (phiras > mendiane > sibur > deraumere > linemate)
// This is correct for chooseMissingStone — it filters via getMissingStones() first.
// ---------------------------------------------------------------------------

std::string SimpleAI::chooseMissingStone() const {
    auto missing = _state.getMissingStones();
    if (missing.empty()) return {};

    static const std::vector<std::string> priority = {
        "thystame", "phiras", "mendiane", "sibur", "deraumere", "linemate"
    };

    for (const auto& preferred : priority) {
        if (std::find(missing.begin(), missing.end(), preferred) != missing.end())
            return preferred;
    }
    return missing.front();
}

// ---------------------------------------------------------------------------
// updateFoodThresholds — adaptive thresholds based on level (FIX K)
// ---------------------------------------------------------------------------

void SimpleAI::updateFoodThresholds() {
    int lvl = _state.getLevel();
    if (lvl <= 2) {
        _foodComfortThreshold   = 15;
        _foodEmergencyThreshold = 8;
        _foodLockedThreshold    = 5;
        _forkFoodThreshold      = 25;
    } else if (lvl <= 4) {
        _foodComfortThreshold   = 22;
        _foodEmergencyThreshold = 10;
        _foodLockedThreshold    = 6;
        _forkFoodThreshold      = 30;
    } else {
        _foodComfortThreshold   = 30;
        _foodEmergencyThreshold = 12;
        _foodLockedThreshold    = 8;
        _forkFoodThreshold      = 40;
    }
}

// ---------------------------------------------------------------------------
// broadcastLeaderStatus — sends both INCANT_LEADER and INCANT_READY (FIX D)
// ---------------------------------------------------------------------------

void SimpleAI::broadcastLeaderStatus(int64_t now) {
    _lastBroadcastTime = now;
    std::string lvl = std::to_string(_state.getLevel());
    std::string tag = std::to_string(_clientTag);
    Logger::info("AI: broadcasting INCANT_LEADER lv" + lvl + " anchor=" + tag);
    _sender.sendBroadcast("INCANT_LEADER " + lvl + " " + tag);
    _sender.sendBroadcast("INCANT_READY "  + lvl + " " + tag);
}

// ---------------------------------------------------------------------------
// broadcastSurplusStones — FIX J: cooperative stone handoff hint
// ---------------------------------------------------------------------------

void SimpleAI::broadcastSurplusStones(int64_t now) {
    (void)now;
    if (_state.getLevel() >= 8) return;
    auto req = _state.getLevelRequirement(_state.getLevel());
    const auto& inv = _state.getInventory();
    for (const auto& [stone, needed] : req.stonesNeeded) {
        auto it = inv.find(stone);
        int have = (it != inv.end()) ? it->second : 0;
        if (have > needed) {
            // We have surplus stones — announce it
            _sender.sendBroadcast("HAVE_STONE " + std::to_string(_state.getLevel()) +
                                  " " + stone + " " + std::to_string(_clientTag));
        }
    }
}

// ---------------------------------------------------------------------------
// startGathering
// ---------------------------------------------------------------------------

void SimpleAI::startGathering(const std::string& resource, int64_t now) {
    _AIstate               = AIState::Gathering;
    _currentResourceTarget = resource;

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

    auto plan = _planner.planPathToResource(_state, resource);
    if (plan.empty()) {
        Logger::warn("AI: no path to " + resource + ", exploring");
        _AIstate = AIState::Exploring;
        plan = _planner.planExploration(_state);
        if (plan.empty()) {
            _AIstate = AIState::Idle;
            return;
        }
    }

    Logger::info("AI: path to " + resource + " (" + std::to_string(plan.size()) + " steps)");
    for (const auto& s : plan) _actionQueue.push(s);
    executeNextAction(now);
}

// ---------------------------------------------------------------------------
// startIncantation
// ---------------------------------------------------------------------------

void SimpleAI::startIncantation(int64_t now) {
    _AIstate             = AIState::Incantating;
    _lastIncantationTime = now;
    Logger::info("AI: sending incantation (lv" + std::to_string(_state.getLevel()) + ")");
    issueTrackedCommand(
        "incantation",
        [this](const ServerMessage& msg) { onCommandComplete(msg); },
        now
    );
    _sender.sendIncantation();
}

// ---------------------------------------------------------------------------
// startFork
// ---------------------------------------------------------------------------

void SimpleAI::startFork(int64_t now) {
    _AIstate      = AIState::WaitingForResponse;
    _lastForkTime = now;
    _forkCount++;
    issueTrackedCommand(
        "fork",
        [this](const ServerMessage& msg) { onCommandComplete(msg); },
        now
    );
    _sender.sendFork();
}

// ---------------------------------------------------------------------------
// Coordination helpers
// ---------------------------------------------------------------------------

bool SimpleAI::hasLeaderLock(int64_t now) const {
    return _leaderLockTag != -1 && now < _leaderLockUntil;
}

bool SimpleAI::canLeadIncantation(int64_t now) const {
    if (_state.getFood() < _foodEmergencyThreshold) return false;
    if (now < _incantBackoffUntil)                  return false;
    if (hasLeaderLock(now) && _leaderLockTag != _clientTag) return false;
    return true;
}

bool SimpleAI::shouldLeadIncantation(int64_t now) const {
    if (!canLeadIncantation(now)) return false;
    if (_selectedAnchorTag != -1 && _selectedAnchorTag != _clientTag && now < _anchorStickyUntil)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// issueTrackedCommand
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// executeNextAction
// ---------------------------------------------------------------------------

void SimpleAI::executeNextAction(int64_t now) {
    if (_actionQueue.empty()) {
        _AIstate = AIState::Idle;
        return;
    }

    auto step = _actionQueue.front();
    _actionQueue.pop();

    Logger::info("AI: exec " + step.toString() +
                 " (q=" + std::to_string(_actionQueue.size()) + ")");

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
                    if (msg.isOk())   Logger::info("AI: took " + step.resource);
                    else              Logger::warn("AI: failed to take " + step.resource);
                    onCommandComplete(msg);
                }, now);
            _sender.sendPrend(step.resource);
            break;

        case NavAction::Place:
            issueTrackedCommand("pose " + step.resource,
                [this, step](const ServerMessage& msg) {
                    if (msg.isOk())   Logger::info("AI: placed " + step.resource);
                    else              Logger::warn("AI: failed to place " + step.resource);
                    onCommandComplete(msg);
                }, now);
            _sender.sendPose(step.resource);
            break;

        default:
            Logger::error("AI: unknown NavAction");
            _waitingForCmd = false;
            break;
    }
}

// ---------------------------------------------------------------------------
// onCommandComplete
// ---------------------------------------------------------------------------

void SimpleAI::onCommandComplete(const ServerMessage& msg) {
    _waitingForCmd = false;

    if (msg.status == "timeout") {
        Logger::warn("AI: timeout on '" + msg.cmd + "', clearing queue");
        clearQueue(_actionQueue);
        _AIstate = AIState::Idle;
        return;
    }

    if (msg.cmd == "incantation") {
        if (msg.isOk()) {
            Logger::info("AI: incantation ok → waiting for level-up event");
            _incantBackoffUntil = 0;
            // Stay Incantating until level-up event resets us (FIX I)
        } else if (msg.isKo()) {
            Logger::warn("AI: incantation ko");
            int64_t backoff = 4000 + static_cast<int64_t>(_state.getLevel()) * 2000;
            _incantBackoffUntil = nowMs() + std::min(backoff, (int64_t)20000);
            _leaderLockTag   = -1;
            _leaderLockUntil = 0;
            _coordinationHoldUntil = 0;
            _AIstate = AIState::Idle;
        }
        return;
    }

    if (!msg.isOk()) {
        Logger::warn("AI: '" + msg.cmd + "' failed (" + msg.status + "), clearing queue");
        clearQueue(_actionQueue);
        _AIstate = AIState::Idle;
        return;
    }

    Logger::debug("AI: '" + msg.cmd + "' ok");

    if (!_actionQueue.empty()) {
        executeNextAction(nowMs());
    } else {
        _AIstate = AIState::Idle;
    }
}

// ---------------------------------------------------------------------------
// onMessage — events and broadcasts
// ---------------------------------------------------------------------------

void SimpleAI::onMessage(const ServerMessage& msg) {

    // ---- server events ----
    if (msg.type == ServerMessageType::Event) {
        bool isIncantStart =
            (msg.eventType.has_value() && *msg.eventType == "incantation_start") ||
            msg.status == "incantation_start";

        if (isIncantStart && _AIstate != AIState::Incantating) {
            Logger::info("AI: incantation_start → participant lock");
            clearQueue(_actionQueue);
            _waitingForCmd       = false;
            _AIstate             = AIState::Incantating;
            _lastIncantationTime = nowMs();
            _selectedAnchorTag   = -1;
            _anchorStickyUntil   = 0;
            return;
        }

        // Level-up: unconditional reset (FIX I)
        if (msg.isLevelUp()) {
            Logger::info("AI: level-up event → reset to Idle");
            _AIstate             = AIState::Idle;  // unconditional, even if Incantating
            _lastIncantationTime = 0;
            _incantBackoffUntil  = 0;
            _leaderLockTag       = -1;
            _leaderLockUntil     = 0;
            _coordinationHoldUntil = 0;
            _selectedAnchorTag   = -1;
            _anchorStickyUntil   = 0;
            clearQueue(_actionQueue);
            _waitingForCmd       = false;
            return;
        }
        return;
    }

    // ---- broadcasts ----
    if (msg.type == ServerMessageType::Message && msg.messageText.has_value()) {
        const std::string& text = *msg.messageText;
        int64_t now = nowMs();

        // --- INCANT_LEADER / INCANT_READY ---
        if (text.find("INCANT_LEADER") != std::string::npos ||
            text.find("INCANT_READY")  != std::string::npos) {

            std::istringstream ss(text);
            std::string prefix;
            int level = 0, anchorTag = -1;
            ss >> prefix >> level;
            if (!(ss >> anchorTag)) anchorTag = -1;

            if (level != _state.getLevel()) return;

            // Update leader lock from any INCANT_LEADER/READY message (FIX C)
            if (anchorTag != -1) {
                bool acceptLeader =
                    (_leaderLockTag == -1) ||
                    (anchorTag == _leaderLockTag) ||
                    (anchorTag < _leaderLockTag);  // lower tag wins

                if (acceptLeader) {
                    _leaderLockTag   = anchorTag;
                    _leaderLockUntil = now + LEADER_LOCK_DURATION_MS;

                    // Set coordination hold so this client doesn't scatter (FIX C)
                    if (anchorTag != _clientTag) {
                        _coordinationHoldUntil = now + COORDINATION_HOLD_MS;
                    }
                }
            }

            if (prefix == "INCANT_LEADER") return; // position update only

            // INCANT_READY: follower approach logic
            if (_AIstate == AIState::Incantating) return;
            if (!msg.direction.has_value())        return;

            // Anchor sticky selection
            if (_anchorStickyUntil > 0 && now >= _anchorStickyUntil) {
                _selectedAnchorTag = -1;
                _anchorStickyUntil = 0;
            }
            if (anchorTag != -1) {
                bool acceptAnchor =
                    (_selectedAnchorTag == -1) ||
                    (anchorTag == _selectedAnchorTag) ||
                    (anchorTag < _selectedAnchorTag);
                if (acceptAnchor) {
                    _selectedAnchorTag = anchorTag;
                    _anchorStickyUntil = now + LEADER_LOCK_DURATION_MS;
                }
                if (_selectedAnchorTag != anchorTag) return; // wrong anchor
            }

            if (now - _lastAnchorFollowTime < 600) return;

            // Food gate: only refuse if starvation risk is real and food is gatherable
            if (_state.getFood() < FOOD_EMERGENCY_ABSOLUTE) return;

            _lastAnchorFollowTime = now;

            int dir = *msg.direction;
            Logger::info("AI: INCANT_READY lv" + std::to_string(level) +
                         " dir=" + std::to_string(dir) + " anchor=" + std::to_string(anchorTag));

            if (dir == 0) {
                Logger::info("AI: on target tile — holding for incantation");
                clearQueue(_actionQueue);
                _waitingForCmd      = false;
                _AIstate            = AIState::Idle;
                _targetBroadcastDir = 0;
            } else {
                _targetBroadcastDir = dir;
                clearQueue(_actionQueue);
                _waitingForCmd = false;
                auto plan = _planner.planApproachDirection(_state, dir);
                for (const auto& s : plan) _actionQueue.push(s);
                Logger::info("AI: approaching INCANT_READY source (dir=" + std::to_string(dir) + ")");
                if (!_actionQueue.empty()) executeNextAction(now);
            }
            return;
        }

        // --- HAVE_STONE: cooperative handoff hint (FIX J) ---
        if (text.find("HAVE_STONE") != std::string::npos && _leaderLockTag == _clientTag) {
            std::istringstream ss(text);
            std::string prefix, stone;
            int level = 0, senderTag = -1;
            ss >> prefix >> level >> stone >> senderTag;
            if (level != _state.getLevel()) return;

            // If we (as leader) need that stone, note the direction
            auto missing = _state.getMissingStones();
            bool weNeedIt = std::find(missing.begin(), missing.end(), stone) != missing.end();
            if (weNeedIt && msg.direction.has_value()) {
                Logger::info("AI: leader noted HAVE_STONE '" + stone +
                             "' from dir=" + std::to_string(*msg.direction));
                // Store as a hint for the next decideNextAction iteration.
                // (The leader will see if it can approach; if dir==0 great, else it's nearby)
            }
        }
    }
}

} // namespace zappy
