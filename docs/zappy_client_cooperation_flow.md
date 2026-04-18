# Zappy Client Cooperation Flow — Reference Document

> This document describes the **intended, correct** behavior of the AI client when cooperating
> with peers to perform a multi-player incantation.  It covers every state, every transition
> trigger, every message, and every invariant that must hold for the system to work reliably.
> It is written against the current codebase (April 2026) and should be treated as the
> ground-truth spec when writing tests or debugging.

---

## 0. Vocabulary

| Term | Meaning |
|---|---|
| **tick** | One call to `Behavior::tick(nowMs)`.  At most one command is sent per tick. |
| **command in flight** | `_commandInFlight == true`.  The client is waiting for a server response and does nothing else. |
| **vision stale** | `_staleVision == true`.  We need a fresh `voir` before acting on tile data. |
| **inventory stale** | `_staleInventory == true`.  Same but for `inventaire`. |
| **leader** | The one client the server has promoted via `claim_leader OK` for this level/round. |
| **follower** | Every other client at the same level during that rally round. |
| **rally round** | One full attempt: claim → lead → follow → incantate (or disband). |
| `FOOD_SAFE` | 16.  Minimum food to start a rally or interrupt CollectFood for it. |
| `FOOD_RALLY` | 16.  Minimum food before entering ClaimingLeader (same value, one constant). |
| `FOOD_CRITICAL` | 4.  Emergency threshold; any state must bail to CollectFood if reached. |
| `FOOD_FORK` | 24.  Threshold to trigger egg-laying. |

---

## 1. State Machine Overview

```
                        ┌──────────────────────────────────────────────────────┐
                        │                   TICK ENTRY                         │
                        │  1. if commandInFlight → return                      │
                        │  2. if vision stale   → refreshVision, return        │
                        │  3. if inventory stale → refreshInventory, return    │
                        │  4. dispatch to current AIState                      │
                        └────────────────────┬─────────────────────────────────┘
                                             │
         ┌───────────────────┬───────────────┼────────────────┬─────────────────┐
         ▼                   ▼               ▼                ▼                 ▼
   CollectFood         CollectStones    ClaimingLeader     Leading          MovingToRally
         │                   │               │                │                 │
         │            stones+food OK         │OK              │enough           │dir==0
         │            (food≥FOOD_RALLY) ─────┘   →Leading     │players          ▼
         │                                                    └──────────► Rallying ──► Incantating
         │                                                                      │
         └──── food < FOOD_CRITICAL (from any state) ───────────────────────────┘
```

The full list of `AIState` values:

| State | Who enters it | Purpose |
|---|---|---|
| `CollectFood` | Anyone | Gather food until target reached |
| `CollectStones` | Anyone | Gather stones for current level |
| `Idle` | Anyone | Momentary pass-through; goes straight to CollectStones |
| `ClaimingLeader` | Anyone with all stones + enough food | Race the server for the leader flag |
| `Leading` | Winner of claim_leader | Broadcast RALLY, wait for followers |
| `MovingToRally` | Loser of claim_leader | Navigate toward the leader using broadcast direction |
| `Rallying` | Both leader and follower, on the same tile | Leader places stones + incantates; follower announces HERE |
| `Incantating` | Leader only | Place stones, verify, fire incantation |

---

## 2. State-by-State Detail

### 2.1 CollectFood

**Entry condition:** food below target, OR `_stonesReady == true` and food below `FOOD_RALLY`.

**Target food:**
- If `_stonesReady == true` (stones already collected, now topping up before rally): `FOOD_RALLY`
- Otherwise: `FOOD_SAFE`

**Behaviour each tick:**
1. If food ≥ target → `_stonesReady = false`, go to `CollectStones`.
2. If nourriture on current tile → `prend nourriture`.
3. If nourriture visible → plan path, move one step.
4. Else → exploration step (turn + forward).

**Interruption by RALLY broadcast:**
- If food ≥ `FOOD_SAFE` at the time of the broadcast, the follower **stops eating** and moves to `MovingToRally` (or `Rallying` if direction == 0).
- If food < `FOOD_SAFE`, the broadcast is ignored; the leader is expected to disband and retry later.

**Food constants rationale:**  
Both constants are 16 so that a follower interrupted at `FOOD_SAFE` still meets the `FOOD_RALLY` requirement for the next rally attempt if this one disbands.

---

### 2.2 CollectStones

**Entry condition:** food ≥ `FOOD_CRITICAL`.

**Behaviour each tick (in priority order):**
1. If food < `FOOD_CRITICAL` → `CollectFood`.
2. If food > `FOOD_FORK` AND level ≥ 2 AND fork enabled → fork egg, then back to CollectStones.
3. Compute missing stones for current level via `computeMissingStones()`.
4. If no stones missing:
   - Level needs > 1 player: check food ≥ `FOOD_RALLY`; if not → `CollectFood` with `_stonesReady=true`; if yes → `ClaimingLeader`.
   - Level needs 1 player: → `Incantating` directly (no cooperation needed).
5. If a needed stone is on the current tile → `prend`.
6. Opportunistic food: if food < `FOOD_SAFE` AND nourriture on tile → `prend nourriture`.
7. Navigate to nearest tile with a needed stone.
8. If no stones visible → exploration step.

**RALLY broadcast during CollectStones:** direction is cached into `_broadcastDirection` but the state is **not** interrupted.  The client finishes collecting first.  This is intentional: a follower that drops its stones to run to the leader is useless.

---

### 2.3 ClaimingLeader

**Entry condition:** all stones collected, food ≥ `FOOD_RALLY`.

**Behaviour:**
1. On first entry (`_claimSent == false`): send `claim_leader` to the server, set `_claimSent = true`, `_commandInFlight = true`.
2. Wait (all subsequent ticks return immediately because `_claimSent == true` acts as a hard latch).
3. On `claim_leader` response:
   - **OK** → `_isLeader = true`, init Leading state vars, go to `Leading`.
   - **KO** → `_isLeader = false`, go to `MovingToRally`.
     - If `_broadcastDirection` is still ≤ 0 (no RALLY received yet), set it to -1 (unknown).
     - If a positive direction was already cached from a RALLY heard during Claiming, preserve it.

**Key invariant:** `_claimSent` is reset to `false` in the response callback regardless of OK/KO, so the next rally attempt can send a fresh claim.

---

### 2.4 Leading

**Entry condition:** `claim_leader` returned OK.

**Init (runs once, when `_leadingTimeoutMs == 0`):**
- Record `_leadingTimeoutMs = nowMs`.
- Set `_lastRallyBroadcastMs = nowMs - 600` so the first broadcast fires immediately.
- Set `_rallyBroadcastCount = 0`.
- **Do NOT return** — fall through to the broadcast block on the same tick.

**Behaviour each tick:**
1. If elapsed > 30 s → `disbandRally(true)`, go to `Idle`.
2. If food < `FOOD_SAFE` → `disbandRally(true)`, go to `CollectFood`.
3. If ≥ 500 ms since last broadcast → send `RALLY:<level>`, increment `_rallyBroadcastCount`.
4. If `_rallyBroadcastCount < 3` → return (wait for more broadcasts before checking playerCount).
5. If vision stale → `refreshVision`.
6. If `vision[0].playerCount >= req.players` → go to `Rallying` (`_isRallying = false`).
7. Else → `setVisionStale()` (loop back to refresh and recheck).

**Why the `_rallyBroadcastCount < 3` guard?**  
Broadcasting is asynchronous.  If the leader checks playerCount before the follower has even heard a single RALLY, it would always see "not enough players" and keep looping.  Three broadcasts give the follower ~1.5 s to hear at least one, update `_broadcastDirection`, and start moving.

**RALLY echo:** The leader receives its own broadcasts echoed back.  The `onBroadcast` handler skips them via `if (_isLeader) return`.

---

### 2.5 MovingToRally

**Entry condition:** `claim_leader` returned KO (follower path).

**Init (runs once, when `_isMovingToRally == false`):**
- Set `_isMovingToRally = true`.
- Record `_movingToRallyTimeoutMs = nowMs`.
- Clear nav plan and nav target.
- Return (no movement this tick).

**Behaviour each tick:**
1. If elapsed > 30 s → `disbandRally(false)`, go to `Idle`.
2. If food < `FOOD_CRITICAL` → `disbandRally(false)`, go to `CollectFood`.
3. **If `_broadcastDirection == -1` (no RALLY heard yet):**
   - If food < `FOOD_SAFE` AND nourriture on current tile → pick it up.
   - Else → take one exploration step.
   - Return.  Do NOT stand still.
4. If `_broadcastDirection == 0` (already on leader's tile) → go to `Rallying`.
5. Else → call `Navigator::planApproachDirection(_broadcastDirection, orientation)` to get turn+forward commands, execute next command.

**Direction update:**  
Every time a RALLY broadcast arrives while in `MovingToRally`, `onBroadcast` updates `_broadcastDirection`.  The nav plan is cleared only if the direction changed, and a fresh approach plan is computed on the next tick.  This is how the follower corrects its course as it receives updated bearings.

**Critical rule:** When `_broadcastDirection` is a positive number (1-8), the follower executes **one move per tick** using `planApproachDirection`.  After each move, vision goes stale, a new `voir` happens, and then the next tick may receive another RALLY with an updated direction.  This is intentional: each step re-evaluates the heading.

---

### 2.6 Rallying

**Both leader and follower enter this state**, but they behave differently.

**Init (runs once, when `_isRallying == false`):**
- Set `_isRallying = true`.
- Record `_rallyingTimeoutMs = nowMs`.
- `setVisionStale()` — need fresh tile info.
- Return.

**Follower path (`!_isLeader`):**
1. If `_broadcastDirection != 0` (leader moved!) → reset `_isRallying`, go back to `MovingToRally`.
2. Else → send `HERE:<level>` broadcast and loop.

**Leader path (`_isLeader`):**
1. If timeout → `disbandRally(true)`, `Idle`.
2. If `vision[0].playerCount >= req.players` → go to `Incantating` (`_incantationReady=false, _stonesPlaced=false`).
3. Else → send another `RALLY:<level>` every 500 ms to help late-arriving followers find the tile.

**HERE message handling (in `onBroadcast`):**
- Only the leader processes HERE.
- `_peerConfirmedCount++`
- If `_peerConfirmedCount >= req.players - 1` → `_aiState = Rallying, _isRallying = false` (re-init to trigger fresh vision on next tick, then recount).
- The leader then checks playerCount from vision in `tickRallying` rather than trusting `_peerConfirmedCount` alone, as HERE can arrive multiple times from the same follower.

---

### 2.7 Incantating

**Entry condition:** Leader only, enough players on tile.

**Four-step sequence (each step is one or more ticks):**

**Step 1 — Fresh vision:**  
`setVisionStale()`, set `_incantationReady = true`, return.  (Ensures we see the actual tile contents.)

**Step 2 — Place stones:**  
For each required stone type (from the level table), if the tile doesn't have enough: `pose <stone>`.  Each `pose` is one tick.  When all stones are placed, set `_stonesPlaced = true`, `setVisionStale()`.

**Step 3 — Verify stones:**  
Wait for vision to refresh.  Check that all required stones are on the tile.  If any are missing (e.g. another player picked them up): reset to CollectStones.

**Step 4 — Fire incantation:**  
Send `incantation`.  The server first sends an `in_progress` response (ignored), then `ok` or `ko` when done.
- **OK + `_pendingLevelUp`** → `_state.player.level++`, go to `CollectStones` (or `Idle` if level 8).
- **KO / timeout** → go to `CollectStones`.
- Either way: call `sendDisbandLeader()` to release the server flag.

---

### 2.8 Disband

`disbandRally(bool wasLeader)` is a cleanup helper called from any state.

**It always:**
- Resets all rally flags: `_stonesReady, _claimSent, _isLeader, _isMovingToRally, _isRallying, _peerConfirmedCount, _rallyLevel, _rallyBroadcastCount`.
- Resets `_broadcastDirection = -1`.
- Clears nav plan.

**If `wasLeader`:**
- Sends `disband_leader` to the server (releases the flag so another client can claim it).
- Sets `_ignoreDone = true` immediately (before the broadcast) so our own DONE echo is not misprocessed.
- Sends `DONE:<level>` broadcast so followers know to give up.
- Sets `_commandInFlight = true` (waits for `disband_leader` response to clear it).
- In the `disband_leader` callback: `_commandInFlight = false`, `_ignoreDone = false`.

**DONE message handling (in `onBroadcast`):**
- Ignored if `_isLeader || _ignoreDone`.
- Only acted upon if in `MovingToRally` or `Rallying`.
- Action: `disbandRally(false)`, go to `CollectStones`.

---

## 3. Broadcast Protocol Summary

| Message | Sender | Receiver action |
|---|---|---|
| `RALLY:<level>` | Leader (Leading + Rallying) | Cache direction; if food OK and in right state → MovingToRally or Rallying |
| `HERE:<level>` | Follower (Rallying only) | Leader increments peer count; if enough → re-init Rallying for fresh vision |
| `DONE:<level>` | Leader (during disbandRally) | Follower in MovingToRally/Rallying → disbandRally + CollectStones |

---

## 4. Food Safety Invariants

These must hold at all times for clients to survive:

| Situation | Rule |
|---|---|
| Any state | If food < `FOOD_CRITICAL` (4), abandon everything and CollectFood |
| Leader in Leading | If food < `FOOD_SAFE` (16), disband and CollectFood |
| Follower in MovingToRally | If food < `FOOD_CRITICAL`, disband and CollectFood |
| Follower waiting for first RALLY | Do NOT spin on `setVisionStale()`.  Explore or pick up food. |
| Both clients simultaneously in CollectFood | The RALLY broadcast from the leader must interrupt the follower **if food ≥ FOOD_SAFE**. |

---

## 5. The Coordination Deadlock (Historical Bugs)

Understanding past failure modes helps avoid reintroducing them.

### 5.1 Symmetric starvation (fixed in Round 1)
Both clients finished collecting stones at roughly the same time.  Both went to `CollectFood` with `_stonesReady = true`.  The leader claimed leadership and started broadcasting, but the follower was in `CollectFood` and the broadcast was **completely ignored** for that state.  The leader starved before the follower responded.

**Fix:** RALLY is now processed during `CollectFood` if food ≥ `FOOD_SAFE`.

### 5.2 Follower spin-starvation (fixed in Round 1)
The follower entered `MovingToRally` with `_broadcastDirection == -1` (no RALLY received yet) and the original code called `setVisionStale()` in a tight loop — burning ticks doing `voir` over and over with no movement, while food slowly depleted.

**Fix:** While `_broadcastDirection == -1`, the follower explores or picks up food instead of spinning.

### 5.3 DONE self-echo (fixed in Round 2)
After `disbandRally(true)`, `_isLeader` was reset to `false` before the DONE broadcast echo returned.  The client then received its own DONE and re-disbanded.

**Fix:** `_ignoreDone = true` is set before sending DONE; cleared only in the `disband_leader` callback.

### 5.4 Leader food drain (ongoing)
The leader stands still broadcasting every 500 ms.  At `-f 10` (one time unit = 10 ms), food drains at 1 unit per 1.26 s real time.  A 30-second timeout means the leader can lose ~24 food units.  Starting with 21 food and `FOOD_SAFE = 16`, the leader disbands at ~7 s of leading.  If the follower is still collecting food, it misses the entire window.

**The real fix:** The two clients must be **ready at roughly the same time**.  The food-check gate before `ClaimingLeader` (`FOOD_RALLY`) must be low enough that both clients reach it quickly but high enough to survive the rally.

### 5.5 Nav plan stale direction (ongoing)
After each step in `MovingToRally`, `_navPlan` may still contain commands computed from an old bearing.  The follower may walk in a wrong direction for several steps before the next RALLY updates `_broadcastDirection`.

The current implementation clears `_navPlan` only when `_navTarget` changes.  For the rally approach, `_navPlan` should be cleared **on every new RALLY broadcast** so the follower immediately recomputes from the latest direction.

---

## 6. Known Outstanding Issues (as of this writing)

1. **`_navPlan` not cleared on direction update in `MovingToRally`.**  The follower executes stale nav commands between RALLY broadcasts.  `onBroadcast` for RALLY should always call `clearNavPlan()` when in `MovingToRally`.

2. **Leader vision check for playerCount is unreliable.**  The leader checks `vision[0].playerCount` but `voir` is expensive and slow.  By the time vision arrives, the follower may have already moved away.  A HERE-count-based approach is more reliable for the final go/no-go decision.

3. **No follower food guard during Rallying.**  The follower sends HERE in a loop but has no food check; if the leader stalls for 30 s the follower can starve.

4. **`_peerConfirmedCount` never resets between HERE messages.**  The same follower may send HERE multiple times.  The leader will think "all peers confirmed" based on a repeat broadcast from one client.  This is currently masked by the playerCount check in `tickRallying`, but it creates confusing logs.

---

## 7. Sequence Diagram — Happy Path (2 clients, level 2→3)

```
Client A (becomes Leader)          Server                Client B (becomes Follower)
─────────────────────────         ────────              ──────────────────────────────
CollectFood → CollectStones                              CollectFood → CollectStones
[has linemate+deraumere+sibur,                           [has same stones, food≥16]
 food≥16]
send claim_leader ──────────────► [leader flag: A]
                  ◄──────────────  ok
→ Leading                                                send claim_leader ──────────►
                                                                           ◄──────────  ko
                                                         → MovingToRally (_broadcastDir=-1)
                                                           (explores while waiting)
send RALLY:2 ────────────────────────────────────────► onBroadcast(dir=3)
                                                         _broadcastDirection = 3
                                                         → still MovingToRally
send RALLY:2 ────────────────────────────────────────► onBroadcast(dir=2)
                                                         turn+forward toward leader
send RALLY:2 ────────────────────────────────────────► onBroadcast(dir=1)
                                                         forward
send RALLY:2 ────────────────────────────────────────► onBroadcast(dir=0)
                                                         _broadcastDirection = 0
                                                         → Rallying
                                                         send HERE:2 ────────────────►
onBroadcast(HERE:2)                                      (keeps sending HERE:2 each tick)
_peerConfirmedCount = 1
→ tickRallying checks vision[0].playerCount ≥ 2
→ Incantating
  pose linemate
  pose deraumere
  pose sibur
  send incantation ──────────────► [server checks: 2 players,
                   ◄──────────────   stones ok] → in_progress
                   ◄──────────────  ok (level_up event to all on tile)
_pendingLevelUp=true → level 3!
send disband_leader
→ CollectStones (for level 3)                            (receives level_up, level 3)
                                                         → CollectStones (for level 3)
```

---

*End of document.*
