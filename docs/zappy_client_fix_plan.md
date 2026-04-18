# Zappy Client Cooperation — Fix Plan & Test Roadmap

> This document is the actionable companion to `zappy_client_cooperation_flow.md`.
> It describes exactly what to change, in what order, and how to verify each change
> before moving on.  Steps are ordered by risk: cheapest/safest changes first,
> structural changes last.

---

## Guiding Principle

We are NOT adding more state.  The state machine is correct in shape.  What is broken is
**three specific, isolated behaviours**:

1. The follower's nav plan goes stale between RALLY broadcasts.
2. The leader's playerCount check is unreliable (races with vision latency).
3. The `_peerConfirmedCount` counter double-fires from repeated HERE messages.

Fix these three things in order.  Run a probe after each one.

---

## Step 0 — Establish a clean baseline probe

Before touching any code, run the two-client normal probe and confirm you see the
current failure signature in the logs:

**Expected failure signature (current state):**
```
[Client B] Behavior: claim_leader KO — entering MovingToRally as follower
[Client B] (no "RALLY dir=" log, or appears very late)
[Client A] Behavior: Leading — food critical, disbanding    ← leader starves
```
or
```
[Client A] Behavior: Leading — enough players (2/2), moving to Rallying
[Client A] Client exited. Final level: 2                   ← server killed mid-incantation
```

**Success signature (target):**
```
[Client A] Behavior: Rallying — enough players (2/2), incantating
[Client A] Level up! Now level 3
[Client B] Level up! Now level 3
```

Save the logs.  Label them `baseline_before_fixes`.

---

## Step 1 — Fix: Clear nav plan on every RALLY direction update in MovingToRally

### The problem

In `onBroadcast`, when a RALLY arrives while in `MovingToRally`, we update
`_broadcastDirection` but we do **not** clear `_navPlan`.  The follower then continues
executing commands computed from the **previous** direction.  On a 10×10 map this means
the follower can walk away from the leader for 1-3 steps before the next broadcast
corrects it.

### The fix

In `onBroadcast`, inside the `RALLY:` handler, after updating `_broadcastDirection`,
add `clearNavPlan()` whenever we are in `MovingToRally`:

```cpp
// In onBroadcast, RALLY: handler, after:
_broadcastDirection = direction;

// ADD THIS:
if (_aiState == AIState::MovingToRally) {
    clearNavPlan();   // recompute approach from the fresh direction next tick
}
```

That's the entire change.  Do not touch anything else.

### Test: gtest — `RallyNavPlanClearedOnDirectionUpdate`

```cpp
TEST_F(BehaviorTest, RallyNavPlanClearedOnDirectionUpdate) {
    // Setup: follower is in MovingToRally with direction=3 (right)
    state.player.level = 2;
    state.player.inventory = { 0,1,1,1,0,0,0 };  // all stones for level 2

    // Get into MovingToRally
    giveFreshVision({ makeTile(0,0,{}) });
    giveFreshInventory(state.player.inventory);
    // Force state
    // ... (see note below on forcing state in tests)

    // First RALLY → direction 3
    ServerMessage rally3;
    rally3.type = MsgType::Broadcast;
    rally3.messageText = "RALLY:2";
    rally3.broadcastDirection = 3;
    behavior.onBroadcast(rally3);

    // Tick → generates a nav plan (turn right + forward)
    giveFreshVision({ makeTile(0,0,{}) });
    behavior.tick(1000);
    // nav plan is now non-empty (has TurnRight)

    // Second RALLY → direction 1 (straight ahead, different)
    ServerMessage rally1;
    rally1.type = MsgType::Broadcast;
    rally1.messageText = "RALLY:2";
    rally1.broadcastDirection = 1;
    behavior.onBroadcast(rally1);

    // Nav plan should be cleared — next tick will recompute for direction 1
    behavior.tick(1100);
    // First command for direction 1 is Forward (no turns needed)
    EXPECT_EQ(sender.lastCmd, "avance");
}
```

**Note on forcing state in tests:**  
The cleanest way is to add a `setAIState(AIState s)` method on `Behavior` guarded by
`#ifdef TESTING`.  Alternatively, drive the behavior through a full sequence of
mocked server responses to reach `MovingToRally` naturally (see Step 4 for a full
integration helper).

### Probe verification

Run the two-client probe.  In the logs you should now see the follower correcting its
heading on each RALLY rather than overshooting:
```
[Client B] Behavior: RALLY dir=5 → MovingToRally     ← first signal
[Client B] (moves one step)
[Client B] Behavior: RALLY dir=3 (direction updated)
[Client B] (moves one step in corrected direction)
[Client B] Behavior: RALLY dir=0 → on leader's tile, Rallying
```

---

## Step 2 — Fix: Use HERE count as the primary trigger to move to Incantating

### The problem

The leader transitions from `Leading` to `Rallying`, and from `Rallying` to `Incantating`,
based on `vision[0].playerCount`.  But:

- `voir` takes server-time units to execute.
- By the time the vision response arrives, the follower may have stepped away momentarily.
- playerCount includes the leader itself, so `>= 2` for a 2-player incantation means both
  must be on the same tile at the exact moment the `voir` executes server-side.

This causes the leader to see playerCount=1 even when the follower is broadcasting HERE
from the same tile, because the vision snapshot and the follower's position are slightly
out of sync.

### The fix

**In `tickLeading`:** Switch from vision-based check to HERE-count-based check as the
primary transition to `Rallying`.

The leader already increments `_peerConfirmedCount` in `onBroadcast` when a HERE arrives.
Use that as the trigger:

```cpp
// In tickLeading, replace the playerCount vision check block:
// OLD:
//   if (_state.vision[0].playerCount >= req.players) { → Rallying }
//   else { setVisionStale(); }

// NEW: use HERE count as primary signal; vision as backup only
const auto& req = levelReq(_state.player.level);
if (_peerConfirmedCount >= req.players - 1) {
    Logger::info("Behavior: Leading — all peers confirmed via HERE, moving to Rallying");
    _peerConfirmedCount = 0;   // reset for next round
    _aiState = AIState::Rallying;
    _isRallying = false;
} else {
    setVisionStale();
}
```

**In `tickRallying` (leader path):** Similarly, use `_peerConfirmedCount` as the
trigger to move to `Incantating` (keep the vision check as a secondary confirmation):

```cpp
// Leader path in tickRallying:
const auto& req = levelReq(_state.player.level);
bool enoughPlayers = (_peerConfirmedCount >= req.players - 1) ||
                     (!_state.vision.empty() && _state.vision[0].playerCount >= req.players);

if (enoughPlayers) {
    _peerConfirmedCount = 0;
    _aiState = AIState::Incantating;
    _incantationReady = false;
    _stonesPlaced = false;
} else {
    // broadcast RALLY every 500ms and refresh vision
    ...
}
```

### Fix: Reset `_peerConfirmedCount` properly between HERE bursts

The follower sends HERE on **every tick** while in Rallying.  The leader gets many HERE
messages from the same client.  Instead of accumulating blindly, track unique sender IDs
— or more simply, reset `_peerConfirmedCount` each time the leader re-enters Rallying
(via the `_isRallying = false` path in `onBroadcast` for HERE):

```cpp
// In onBroadcast, HERE: handler:
if (!_isLeader || level != _state.player.level) return;

// Only count once per "rally round" entry — ignore if already confirmed
// The count resets when we re-enter Rallying (_isRallying = false reset)
_peerConfirmedCount++;
Logger::info("Behavior: peer HERE (total=" + std::to_string(_peerConfirmedCount) + ")");

const auto& req = levelReq(_state.player.level);
if (_peerConfirmedCount >= req.players - 1) {
    Logger::info("Behavior: all peers confirmed → Rallying");
    _aiState = AIState::Rallying;
    _isRallying = false;
    // Do NOT reset _peerConfirmedCount here — tickRallying reads it
}
```

And in `disbandRally`, which already resets `_peerConfirmedCount = 0`, that handles cleanup.

Also reset it explicitly when the leader transitions into Rallying from Leading:
```cpp
// In tickLeading, when transitioning:
_peerConfirmedCount = 0;
_aiState = AIState::Rallying;
_isRallying = false;
```

### Test: gtest — `LeaderTransitionsOnHereCount`

```cpp
TEST_F(BehaviorTest, LeaderTransitionsOnHereCount) {
    // Setup: leader in Leading state, 3 RALLY broadcasts done
    state.player.level = 2;
    // ... force to Leading state with _isLeader=true, _rallyBroadcastCount=3

    // No vision yet — playerCount would be 1
    VisionTile tile = makeTile(0,0,{});
    tile.playerCount = 1;   // only leader visible
    giveFreshVision({tile});

    // Simulate follower sending HERE
    ServerMessage here;
    here.type = MsgType::Broadcast;
    here.messageText = "HERE:2";
    here.broadcastDirection = 0;
    behavior.onBroadcast(here);

    // Leader should now be in Rallying (HERE count = 1 = req.players-1 for level 2)
    EXPECT_EQ(behavior.getState(), AIState::Rallying);
}
```

### Test: gtest — `HereCountResetsOnDisband`

```cpp
TEST_F(BehaviorTest, HereCountResetsOnDisband) {
    state.player.level = 2;
    // force _isLeader=true, inject 3 HERE messages
    for (int i = 0; i < 3; i++) {
        ServerMessage here;
        here.messageText = "HERE:2";
        here.broadcastDirection = 0;
        behavior.onBroadcast(here);
    }
    // Now disband
    behavior.disbandRally(true);   // make disbandRally public or friend for testing
    // peerConfirmedCount should be 0
    // On next claim, fresh count
    // (verify via next HERE handling — leader should need 1 HERE again)
}
```

### Probe verification

After this fix, look for:
```
[Client A] Behavior: peer HERE (total=1)
[Client A] Behavior: all peers confirmed → Rallying
[Client A] Behavior: Rallying — enough players (2/2), incantating
```
The leader should no longer get stuck in a voir loop failing to see 2 players.

---

## Step 3 — Fix: Lower FOOD_RALLY / align food constants

### The problem

`FOOD_RALLY = 24` causes both clients to spend ~8 extra seconds collecting food before
attempting the rally.  During that time, one may claim leadership and start broadcasting
while the other is still eating.  With `FOOD_SAFE = 16`, the follower can be interrupted
mid-food-collect at food=16 — but then `FOOD_RALLY = 24` means the **food target for the
interrupted client drops back to `FOOD_SAFE`**, since the RALLY path skips the food check.

The cleanest solution: **unify `FOOD_RALLY = FOOD_SAFE = 16`**.  This means:
- Both clients enter ClaimingLeader at food=16.
- The leader starts with 16 food and has a comfortable buffer for the rally (~10 s at game speed).
- After disbanding, the threshold to re-enter is also 16, so recovery is fast.

### The change

In `Behavior.hpp`:
```cpp
static constexpr int FOOD_RALLY    = 16;  // was 24
static constexpr int FOOD_SAFE     = 16;
static constexpr int FOOD_CRITICAL = 4;
static constexpr int FOOD_FORK     = 24;
```

### Test: gtest — `EntersClaimingLeaderAtFoodRally`

```cpp
TEST_F(BehaviorTest, EntersClaimingLeaderAtFoodRally) {
    state.player.level = 2;
    // Give level-2 stones
    state.player.inventory = { 16, 1, 1, 1, 0, 0, 0 };  // food=16, has linemate+deraumere+sibur

    giveFreshVision({ makeTile(0,0,{}) });
    giveFreshInventory(state.player.inventory);

    behavior.tick(0);
    // Should go to ClaimingLeader, sending claim_leader
    EXPECT_EQ(sender.lastCmd, "claim_leader");
    EXPECT_EQ(behavior.getState(), AIState::ClaimingLeader);
}
```

```cpp
TEST_F(BehaviorTest, StaysInCollectFoodIfFoodBelowRally) {
    state.player.level = 2;
    state.player.inventory = { 14, 1, 1, 1, 0, 0, 0 };  // food=14 < 16

    giveFreshVision({ makeTile(0,0,{}) });
    giveFreshInventory(state.player.inventory);

    behavior.tick(0);
    // Should still be in CollectFood (or CollectStones → CollectFood transition)
    EXPECT_NE(behavior.getState(), AIState::ClaimingLeader);
}
```

### Probe verification

Both clients should now reach `ClaimingLeader` at roughly the same time (within 2-3 s
of each other), because they're both targeting food=16 rather than 24.

---

## Step 4 — Integration test: Full 2-client rally simulation in gtest

This is the most valuable test.  It simulates the complete cooperation flow for two
`Behavior` instances talking through fake senders, without a real server.

### Setup

Create `BehaviorCoopTest.cpp` (new file) with a fixture that has two behavior instances
sharing a simulated broadcast channel:

```cpp
class BehaviorCoopTest : public ::testing::Test {
    MockWebsocketClient wsA, wsB;
    FakeSender senderA{wsA}, senderB{wsB};
    WorldState stateA, stateB;
    Behavior behaviorA{senderA, stateA};
    Behavior behaviorB{senderB, stateB};

    // Deliver a broadcast from A to B (simulate server echo)
    void broadcastAtoB(const std::string& text, int direction) {
        ServerMessage msg;
        msg.type = MsgType::Broadcast;
        msg.messageText = text;
        msg.broadcastDirection = direction;
        behaviorB.onBroadcast(msg);
    }

    void broadcastBtoA(const std::string& text, int direction) {
        ServerMessage msg;
        msg.type = MsgType::Broadcast;
        msg.messageText = text;
        msg.broadcastDirection = direction;
        behaviorA.onBroadcast(msg);
    }
};
```

### Test: `TwoClientsReachIncantating`

```cpp
TEST_F(BehaviorCoopTest, TwoClientsReachIncantating) {
    // Give both clients level 2 with all required stones and food=16
    auto readyInventory = Inventory{ 16, 1, 1, 1, 0, 0, 0 };
    stateA.player.level = stateB.player.level = 2;

    // Prime both with fresh vision+inventory
    // ... (use giveFreshVision/giveFreshInventory helpers for both)

    // Tick A: goes to ClaimingLeader, sends claim_leader
    behaviorA.tick(0);
    ASSERT_EQ(senderA.lastCmd, "claim_leader");

    // Server says A is leader
    senderA.fireCallback(makeOkResponse("claim_leader"));
    ASSERT_EQ(behaviorA.getState(), AIState::Leading);

    // Tick B: also goes to ClaimingLeader
    behaviorB.tick(0);
    ASSERT_EQ(senderB.lastCmd, "claim_leader");

    // Server says B is NOT leader
    senderB.fireCallback(makeKoResponse("claim_leader"));
    ASSERT_EQ(behaviorB.getState(), AIState::MovingToRally);

    // A broadcasts RALLY:2 (from tickLeading)
    behaviorA.tick(100);
    ASSERT_EQ(senderA.lastCmd, "broadcast");
    // Deliver to B with direction=1 (straight ahead)
    broadcastAtoB("RALLY:2", 1);
    ASSERT_EQ(behaviorB._broadcastDirection, 1);   // or use a getter

    // B moves toward A (avance)
    behaviorB.tick(200);
    ASSERT_EQ(senderB.lastCmd, "avance");
    senderB.fireCallback(makeOkResponse("avance"));

    // A broadcasts another RALLY:2, now direction=0 (B is on A's tile)
    broadcastAtoB("RALLY:2", 0);
    ASSERT_EQ(behaviorB.getState(), AIState::Rallying);

    // B sends HERE:2
    behaviorB.tick(300);
    ASSERT_EQ(senderB.lastCmd, "broadcast");   // HERE:2
    broadcastBtoA("HERE:2", 0);

    // A: peerConfirmedCount=1 → transitions to Rallying
    ASSERT_EQ(behaviorA.getState(), AIState::Rallying);

    // A: tickRallying sees enough players → Incantating
    behaviorA.tick(400);
    ASSERT_EQ(behaviorA.getState(), AIState::Incantating);
}
```

This test is the single most important one.  If it passes, the entire cooperation flow
is verified without needing a live server.

---

## Step 5 — Probe: Full two-client run to level 3

Run `run_test_two_clients_normal.sh`.  The success criteria are:

```
✓ Both clients log "Level up! Now level 3"
✓ Neither client logs "Client exited. Final level: 2"
✓ No "Leading timed out" or "Leading — food critical" before the level-up
✓ The follower logs at least one "RALLY dir=X → MovingToRally" before "RALLY dir=0 → Rallying"
✓ The leader logs "Behavior: Rallying — enough players (2/2), incantating"
```

If `Level up! Now level 3` appears for both: **done**.  If not, go to Step 6.

---

## Step 6 — Contingency: If the leader still starves

If logs show the leader disbanding due to food before the follower arrives, the problem
is timing: the follower is too slow to navigate to the leader within the food budget.

**Diagnosis:** Look at the time between `Behavior: Leading — level 2` and `food critical, disbanding`.
Compare it to the time the follower takes to reach the leader's tile.

**Fix options (pick one):**

### Option A — Increase leader's food guard back to FOOD_CRITICAL
```cpp
// In tickLeading:
if (_state.player.food() < FOOD_CRITICAL) {  // was FOOD_SAFE
```
This gives the leader much more time (until food=4) before disbanding.  The risk is the
leader might die if it waits too long — but at `-f 10` on a 10×10 map the food rate is
slow enough that 4 food is still safe for a few more seconds.

### Option B — Leader picks up food while waiting
While in Leading, if there is nourriture on the current tile, pick it up between broadcasts.
This requires restructuring `tickLeading` to check the current tile between broadcast windows.

### Option C — Reduce the broadcast interval to 250ms
The follower navigates faster if it gets more frequent direction updates.
```cpp
static constexpr int RALLY_BROADCAST_INTERVAL_MS = 250;  // was 500
```
More broadcasts = faster convergence = less food burned while waiting.

**Recommendation:** Try Option C first (it's one constant change), then Option A if still failing.

---

## Step 7 — Contingency: If the incantation fires but both clients don't level up

This means the incantation succeeded server-side but the `_pendingLevelUp` flag wasn't
set on the follower.  The follower only levels up if it receives the server's level-up
event, which your message parser must handle separately from the incantation response.

**Diagnosis:** Check if the follower logs "Client exited. Final level: 2" while the leader
logs "Level up! Now level 3".

**Fix:** Ensure your main loop's message parser calls `behavior.setPendingLevelUp(true)` 
when it receives a level-up event from the server — for **all** clients on the tile,
not just the one who initiated the incantation.

---

## Summary Checklist

| Step | Change | How to verify |
|---|---|---|
| 0 | Baseline probe | Confirm current failure signature in logs |
| 1 | `clearNavPlan()` on RALLY direction update in MovingToRally | gtest `RallyNavPlanClearedOnDirectionUpdate` + probe |
| 2a | HERE-count-based transition in Leading and Rallying | gtest `LeaderTransitionsOnHereCount` |
| 2b | Reset `_peerConfirmedCount` on re-entry to Rallying | gtest `HereCountResetsOnDisband` |
| 3 | `FOOD_RALLY = 16` | gtest `EntersClaimingLeaderAtFoodRally` |
| 4 | Integration test: two Behavior instances | gtest `TwoClientsReachIncantating` |
| 5 | Full probe to level 3 | Both clients log "Level up! Now level 3" |
| 6 | Contingency: leader starvation | Choose Option A/B/C based on logs |
| 7 | Contingency: follower doesn't level up | Check level-up event parsing |

---

*End of plan.*
