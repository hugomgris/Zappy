# AI Development Current State Assessment
**Last Updated:** 2026-04-11 | **Status:** Level 3 Progression Blocked

---

## Executive Summary

The Zappy AI system has achieved **stable level 2 progression** in multi-client (12v12) probes but is **completely blocked at level 3**. Bounded probes (60-120s) consistently show:
- **Level 2:** 8-9 players reaching level 2 (stable, working)
- **Level 3:** 0 players (100% failure rate)
- **Incantation KO Rate:** 2-4 per probe (manageable, not primary issue)

Multiple patches have been applied to address identified root causes (alphabetical resource ordering, too-strict leader election gates, follower re-scatter behavior), but the core level-3 synchronization deadlock remains unresolved.

---

## What Works ✅

### Level 2 Progression
- Clients consistently reach level 2 in all probes
- Incantation ceremony executes reliably at level 2
- Group assembly functions adequately for 2-player teams
- Broadcast coordination (INCANT_LEADER/INCANT_READY) works for level 2 scale

### Core Mechanics
- Food gathering and survival logic functional
- Movement between tiles and pathfinding accurate
- Multi-agent coordination at basic level working
- Server integration and message parsing working
- TLS/WebSocket connection stability good

### State Management
- WorldState tracking is accurate (position, inventory, level)
- Vision updates processed correctly
- Command queue execution reliable
- Local AI state machine transitions working

---

## What Fails ❌

### Level 3 Progression (Critical Blocker)
**Symptom:** Exactly 0 players reach level 3 in all test runs, even with 120+ seconds.

**Evidence:**
- 60s probe: 8 level-2 players, 0 level-3 players
- 90s probe: 9 level-2 players, 0 level-3 players  
- 120s probe: 9 level-2 players, 0 level-3 players
- No INCANT_LEADER/INCANT_READY broadcasts observed for level 3 in logs

**Failure Mode:** 
- Clients gather initial stones and reach level 2
- After leveling, clients begin gathering level-3 stone set (phiras, mendiane, sibur, deraumere, linemate)
- Leader election triggers (gate: `_state.getLevel() >= 2 && !_state.hasEnoughPlayers()`)
- Clients move toward anchor tile and assemble
- **At assembly site:** Clients check `hasLeaderLock()` but apparently break formation to continue gathering
- **Result:** No coordinated incantation attempt at level 3; clients perpetually re-scatter

---

## Root Causes Identified

### 1. Alphabetical Resource Bias (PARTIALLY FIXED ✓)
**Problem:** `getMissingStones()` returned list in arbitrary order; code always picked `missing[0]`, which was alphabetically first (linemate).

**Manifestation:** 
- All clients logged "need stone 'linemate'" compulsively
- Higher-tier stones (phiras, mendiane) never gathered despite being requirements
- Level 3 requires phiras/mendiane which clients weren't pursuing

**Fix Applied:**
- Implemented `chooseMissingStone()` helper with priority vector: `[phiras, mendiane, sibur, deraumere, linemate]`
- Updated 3 callsites to use priority-based selector (lines 205, 239, 274)
- **Validation:** Latest probes show sibur/deraumere now appearing in logs (selector confirmed working)
- **BUT:** Still insufficient for level-3 breakthrough (0 level-3 players after patch)

### 2. Too-Strict Leader Election Gate (PARTIALLY FIXED ✓)
**Problem:** Original gate: `_state.hasStonesForIncantation() && !_state.hasEnoughPlayers()`
- Required leader to already possess **all** stones before starting coordination
- By that point, other clients had dispersed; no synchronization window remained
- Upstream cause of follower re-scatter

**Fix Applied:**
- Changed gate to: `_state.getLevel() >= 2 && !_state.hasEnoughPlayers()`
- Allows leader election at level 2+ without requiring perfect stone set
- Leader can broadcast and coordinate while still gathering or accepting handoff
- **BUT:** Followers still scatter when stones are missing (Priority 4 fallback overrides Priority 3 hold)

### 3. Follower Assembly Stability (PARTIALLY ADDRESSED ✓)
**Problem:** After assembling on anchor tile, followers would check for missing stones and scatter instead of holding position.
- Added `hasLeaderLock()` checks at lines 239, 254 to prevent re-scatter
- Followers now supposed to hold position when different client has active leader-lock
- **BUT:** Logic may not be preventing scatter in all paths, or timing/broadcast delay is too tight

### 4. Unknown Deeper Issue (ROOT CAUSE NOT YET IDENTIFIED ❌)
- Resource reordering alone insufficient for level 3
- New selector IS working (proven by sibur/deraumere appearing in logs)
- Level 3 still completely blocked despite all three fixes
- **Hypothesis candidates:** 
  - Stone abundance on generated map insufficient for 12 clients
  - Synchronization window too tight; assembly takes longer than incantation window allows
  - Broadcast delay causing race condition (INCANT_READY arrives after follower idle timeout)
  - followers/leader out-of-sync on stone requirements (version mismatch between clients?)
  - Incantation ceremony requirements strictness at level 3 (exact count check failing?)

---

## Technical Inventory

### Code Changes Applied (Session 2026-04-11)

**File: client_cpp/srcs/app/SimpleAI.cpp**

1. **Line 191** (Coordination Gate)
   - Changed: `&& _state.hasStonesForIncantation()` → `&& _state.getLevel() >= 2`
   - Effect: Allows leader election to start at level 2

2. **Lines 205-210** (Leader Resource Gathering)
   - Changed: `missing[0]` → `chooseMissingStone()`
   - Effect: Leaders now prioritize scarcer resources

3. **Lines 239-244** (Follower Resource Gathering)
   - Added: `hasLeaderLock()` check
   - Changed: Resource selection to use `chooseMissingStone()`
   - Effect: Followers hold position when leader is active

4. **Lines 274-280** (Generic Priority 4 Fallback)
   - Changed: Resource selection to use `chooseMissingStone()`
   - Effect: All resource gathering uses priority heuristic

5. **Lines 381-398** (New Helper Function)
   - Added: `std::string chooseMissingStone() const`
   - Implements priority vector: `[phiras, mendiane, sibur, deraumere, linemate]`

**File: client_cpp/srcs/app/SimpleAI.hpp**

1. **Line 118**
   - Added: Function declaration `std::string chooseMissingStone() const;`

### Build Status
- ✅ Clean compilation after all patches
- ✅ No new warnings or errors
- ✅ Client executable functioning

### Probe Infrastructure
- `run_full_game_probe.sh` has `--max-seconds` parameter (default 900s)
- All probes must use explicit `--max-seconds` flag to prevent infinite hangs
- Established timeout discipline: **60s/90s/120s bounded probes for rapid feedback**

---

## Where We Are: Detailed Analysis

### Decision Tree Execution (SimpleAI::decideNextAction)

**Priority 1 (Food):** Maintained; functional
- If food < threshold, gather nourriture
- Works well; clients sustain themselves

**Priority 2 (Fork Spawning):** Not in use; fork mode disabled
- Gate: `_easyAscensionMode && _state.getLevel() >= 2 && _state.hasEnoughFood()`

**Priority 3 (Coordination Wait - THE PROBLEM)**
```cpp
if (!_easyAscensionMode && _state.getLevel() >= 2 && !_state.hasEnoughPlayers()) {
    // Try to elect as leader and start coordination
    // Broadcast INCANT_LEADER
    // Wait for followers to arrive
}
```
- Gate condition now more permissive (was: `&& _state.hasStonesForIncantation()`)
- **Issue:** Even with broader gate, level 3 coordination never fires successfully
- **Possible cause:** Condition `!_state.hasEnoughPlayers()` might be failing at scale, or broadcast not reaching followers

**Priority 4 (Resource Gathering - OVERRIDES PRIORITY 3)**
```cpp
// Gather missing stones for current level
// NEW: Uses chooseMissingStone() for priority targeting
// NEW: Checks hasLeaderLock() to suppress when leader active (follower mode)
```
- Works at level 2 (clients gradually assemble stones)
- At level 3: Clients may be switching between Priority 3 and Priority 4 too rapidly, preventing stable group formation

**Priority 5 (Emergency):** Fallback; rarely hit

### Log Evidence

**Client behavior after level 2 breakthrough:**
```
[INFO] AI: need stone 'deraumere'  // NEW: Priority-based selection working
[INFO] AI: gathering deraumere with 7 steps
[INFO] AI: moved to (7,3)
[INFO] AI: food=32 → gathering nourriture  // Priority 1 re-triggered mid-coordination
[INFO] AI: gathering nourriture with 8 steps
...
[INFO] AI: moved to (9,6)
// No INCANT_READY observed at level 3 mark
```

**Key observations:**
- Resource selection heuristic confirmed active (sibur/deraumere appearing, not just linemate)
- Clients reaching level 2 successfully and starting to gather level-3 stones
- But then Priority 1 or Priority 4 re-triggers, scattering coordination

---

## Possible Next Steps (Prioritized)

### Priority 1: Quick Validation (Time: ~ 10 min)

**STEP 1a: Run Longer Bounded Probe (180-300s)**
```bash
./run_full_game_probe.sh --max-seconds 300 --team1-clients 12 --team2-clients 12 --server-slots 24 --no-fork
```
- **Goal:** Determine if level 3 eventually succeeds with more time
- **Success indicator:** Any level-3 progression (even 1 player)
- **Interpretation:** 
  - If YES: Coordination just needs more time; consider soft timeouts/backoff in Priority 4
  - If NO: Deeper synchronization issue; move to Step 2

**STEP 1b: Check Latest Probe Output**
```bash
tail -20 logs/full_probe_*/output.txt | grep "lvl3_players"
```
- Gives quick metric on whether level-3 is still at 0

---

### Priority 2: Investigate Map Resource Abundance (Time: ~ 15 min)

**STEP 2a: Inspect Map Stone Distribution**
- Parse server logs from probe start to find initial stone placement
- Count total `phiras`, `mendiane` on map
- Compare against: `12 clients × 1 requirement per level-3 = 12 minimum needed`

**STEP 2b: Hypothesis Test**
- If map has < 12 phiras but probe ran 120+s: **Stone starvation is root cause**
  - Solution: Adjust map generation parameters (server-side)
  - Or clients need to accept "stone handoff" from other clients during incantation
  
- If map has ≥ 12 phiras but still 0 level-3: **Not resource availability**
  - Proceed to Priority 3 (synchronization timing)

---

### Priority 3: Debug Broadcast/Synchronization Timing (Time: ~ 30 min)

**STEP 3a: Add Timestamp Logging**
- Modify SimpleAI to log microsecond timestamps when:
  - Leader broadcasts `INCANT_LEADER` (line 192)
  - Follower receives `INCANT_LEADER` (broadcast handler)
  - Follower arrives at anchor tile (Priority 3 entry)
  - Follower executes Priority 3 (broadcast coordination)
  
**STEP 3b: Measure Real Latencies**
- Extract logs and compute: 
  - Broadcast send → receive latency
  - Resource gather duration
  - Time from first follower arrival to full group assembly
  - Compare against configured `assembly_timeout`

**STEP 3c: Adjust Timeouts if Needed**
- If latency > configured assembly window: Increase timeout in SimpleAI
- If latency < window but broadcast still failing: Investigate broadcast propagation logic

---

### Priority 4: Inspect Incantation Prerequisites (Time: ~ 15 min)

**STEP 4a: Verify Level-3 Requirements**
- Check server config for exact incantation requirements at level 3
- Typical: `2 linemate, 1 deraumere, 2 sibur, 2 mendiane, 1 phiras` (may vary)

**STEP 4b: Trace Incantation Logic**
- Add logging to `_state.hasStonesForIncantation()` to see what each client believes they have
- Verify all 6 clients agree on stone requirement counts
- Check for version skew (some clients using old level-2 requirements?)

**STEP 4c: Manual Incantation Test**
- Create simple test: 6 clients manually position + broadcast incantation at level 2
- Goal: Verify ceremony itself works before debugging assembly logic

---

### Priority 5: Refactor Priority Decision Tree (Time: ~ 2 hours, only if steps 1-4 inconclusive)

**If steps 1-4 show no clear culprit, consider:**
- Implement "time-bounded priority hold": Once leader broadcasts, followers ignore Priority 4 for N seconds (configurable)
- Add exponential backoff to Priority 3 election (if election fails, wait before retrying)
- Implement "stone reserve" logic: Don't spend certain stones if < threshold for level-3 (prevent re-scatter)
- Add debugging telemetry stream to see real-time decision choices from all clients

---

## Culprit Tackles (Attack Vectors)

### Most Likely Culprits (in order of hypothesis confidence)

1. **Broadcast Propagation Delay** (Confidence: 60%)
   - INCANT_LEADER arrives after follower idle timeout
   - Follower falls through to Priority 4 before receiving coordination signal
   - **Tackle:** Add explicit acknowledgment handshake; confirm INCANT_LEADER received before follower scatters

2. **Stone Abundance on Generated Map** (Confidence: 50%)
   - Map generator doesn't guarantee 12+ phiras for 12 clients
   - Stone starvation causes follower re-scatter when searching for phiras
   - **Tackle:** Inspect server log for stone counts; adjust generation parameters if insufficient

3. **Priority 1 (Food) Re-Triggering During Assembly** (Confidence: 45%)
   - Food < threshold checked every loop iteration
   - During long assembly, food naturally depletes (10 food consumed ≈ 10 seconds)
   - **Tackle:** Increase food threshold or disable food check during active coordination window

4. **Incantation Window Timeout** (Confidence: 40%)
   - Server has hard deadline for ceremony completion
   - With 12 clients, assembly takes longer when spread across map
   - Clients don't arrive in time before window closes
   - **Tackle:** Investigate server-side incantation timeout configuration; consider longer window for higher player counts

5. **`hasLeaderLock()` Not Properly Preventing Scatter** (Confidence: 35%)
   - New follower-hold checks added but may not cover all resource-gathering paths
   - Some client might be accepting non-leader broadcasts or looping through Priority 4 somehow
   - **Tackle:** Add instrumentation to log every time Priority 4 is entered during coordination window; trace path

---

## Metrics & Baselines

### Current Probe Results (As of 2026-04-11)

| Probe Duration | Level-2 Players | Level-3 Players | Incantation KO | Status |
|---|---|---|---|---|
| 60s | 8 | 0 | 3 | Timeout |
| 90s | 9 | 0 | 2 | Timeout |
| 120s | 9 | 0 | 4 | Timeout |

**Velocity:** ~1-2 level-2 advancement per 30s; 0 level-3 progression at any duration

### Entry Criteria for "Fixed"
- ✅ Level-3 progression: ≥ 1 player reaches level 3 in ≤180s (bounded probe)
- ✅ Stable rate: ≥ 4 players reach level 3 in 180s probe (consistent progression)
- (Full "fix" = reaching level 8 in 600s probe with both teams progressing in tandem)

---

## Known Unknowns & Assumptions

**Assumptions we're making:**
1. Map generation provides sufficient resources for all levels
2. Broadcast mesh between clients is fully connected (no isolated nodes)
3. Server-side incantation window is adequate for 12-client group
4. All clients run identical SimpleAI logic (version consistency)
5. Time synchronization between server and clients adequate

**Things NOT yet explored:**
- Whether level 3 CAN be reached with fewer clients (e.g., 2v2 test)
- Exact server-side incantation requirements and timeout settings
- Whether broadcast mesh has latency/packet loss issues at scale
- Whether generated map has sufficient resource density
- Whether `_state.getLevel()` is accurately tracking server's view

---

## Recommended Immediate Action

**Run Priority 1 probe + analyze results:**

```bash
# Terminal 1: Run longer probe
cd /home/jareste/hugo/zappy
./run_full_game_probe.sh --max-seconds 300 --team1-clients 12 --team2-clients 12 --server-slots 24 --no-fork

# While running, Terminal 2: Monitor
watch -n 10 'tail -1 logs/full_probe_*/output.txt | grep -E "lvl2_players|lvl3_players"'

# After completion: Analyze
grep "INCANT_LEADER\|INCANT_READY\|level 3" logs/full_probe_*/client_*.log | head -50
```

**Based on results:**
- If level-3 appears: Adjust Priority 1/4 timing and run next probe
- If still 0: Proceed to Priority 2 (map stone abundance check)

---

## Files Modified This Session

- `client_cpp/srcs/app/SimpleAI.cpp` (4 changes + 1 new function)
- `client_cpp/srcs/app/SimpleAI.hpp` (1 new declaration)
- Memory files: `/memories/repo/zappy_facts.md`, `/memories/timeouts.md`

---

## Conclusion

The AI system is functionally capable at level 2 but encounters a **hard synchronization deadlock at level 3**. The bottleneck is NOT:
- ~~Basic mechanics~~ (those work)
- ~~Level 2 progression~~ (proven stable)
- ~~Resource selection entirely~~ (partially fixed; selector now working)

The deadlock IS:
- **Unresolved:** Deeper coordination synchronization issue
- **Manifestation:** 0/12 clients reach level 3 despite correct resource gathering and leadership gate
- **Likely cause:** One of {broadcast latency, food re-trigger during assembly, stone starvation, incantation window too tight}

**Next session should start with Priority 1 probe (300s) to establish if more time helps, then systematically eliminate hypotheses using Priority 2-4 steps.**

---

*Assessment prepared for continuation of AI development. All code references are 1-based line numbers as of build commit [current].*
