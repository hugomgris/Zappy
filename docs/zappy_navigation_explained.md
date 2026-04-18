# Zappy Client Navigation — How It Actually Works

> This document explains the navigation system from first principles: how paths are built,
> why they're executed one step at a time, when they're kept vs discarded, and how the
> whole thing meshes with the behavior state machine.

---

## 1. The Fundamental Constraint: One Command Per Tick

Everything about the navigation design flows from one rule that the server imposes:
**you can only have one command in flight at a time**.  The client sends a command,
the server processes it and replies, and only then can the next command be sent.

This means navigation can never be "fire and forget".  You can't send
`TurnRight, Forward, Forward, TurnLeft, Forward` in a burst and wait for it all to
finish.  You send `TurnRight`, wait for the server's `ok`, then send `Forward`, wait
for `ok`, and so on.

This is why `_navPlan` is a **`std::deque<NavCmd>`** — a queue of pending moves — and
why `Behavior` pops only **one command per tick**:

```cpp
NavCmd next = _navPlan.front();
_navPlan.pop_front();
executeNavCmd(next);   // sends the command, sets _commandInFlight = true
```

The plan is the memory of "what I still need to do to get there".  Executing it is
always one-step-at-a-time, gated by the server's response.

---

## 2. The Three Plan Generators

`Navigator` has three static functions that produce plans.  They are pure functions:
no state, no side effects — they just take some geometry and return a list of commands.

### 2.1 `planPath(facing, localX, localY)` — "go to this tile I can see"

Used when the client has spotted a specific tile in its vision and wants to walk there.
`localX` and `localY` are the tile's coordinates in the **vision coordinate system**:

```
Vision coordinate system (facing North):

         localY=0   (current tile)
         localY=1   (one step ahead)
         localY=2   (two steps ahead)
  localX=-1    localX=0    localX=1
```

So `localX=1, localY=2` means "one tile to the right and two tiles forward from me".

The plan produced is always in this shape:

```
[turns to face X-axis] + [|localX| Forwards] + [turns back to original facing] + [localY Forwards]
```

For example, facing North, target at `localX=2, localY=3`:
1. Turn right (now facing East)
2. Forward, Forward  (moved 2 East)
3. Turn left (back to facing North)
4. Forward, Forward, Forward  (moved 3 North)

**The key thing:** this is a complete, pre-computed route from the current position to
the target tile, computed *at the moment of planning* using the current orientation.
It assumes the world doesn't change during execution.

### 2.2 `explorationStep(stepCount)` — "move somewhere, I have no target"

Used when there's nothing useful visible.  Produces **one** move: sometimes a turn,
always a Forward.  The turn pattern (right on step 1, right every 7 steps, left every
13 steps) creates a loose spiral that covers the map without looping forever.

Notice: `explorationStep` always returns a tiny plan of 1-2 commands, not a long route.
This is intentional: exploration is reactive.  You take one step, look again, and decide
what to do next.  No point planning further when you can't see what's out there.

### 2.3 `planApproachDirection(broadcastDirection, currentFacing)` — "move toward a sound"

Used exclusively in `MovingToRally`.  The server's broadcast direction is a clock-face
number (1-8) telling you which sector the sound came from relative to your facing:

```
        1  (straight ahead)
      8   2
    7       3
      6   4
        5  (behind you)
```

This function maps that coarse bearing to a turn (0, 1, or 2 right-turns) and one
Forward.  It always produces exactly **one move** — turn to face the right quadrant,
step forward.

Unlike `planPath`, this doesn't know how far away the target is.  It only knows
"roughly that direction".  You take one step, wait for the next RALLY broadcast to
update the bearing, and plan again.  It's dead-reckoning with corrections.

---

## 3. The Nav Plan Is Not Executed All At Once

This is the part that trips people up.  When you call `Navigator::planPath(...)` for a
tile at `localX=2, localY=3` facing North, you get back something like:

```
[TurnRight, Forward, Forward, TurnLeft, Forward, Forward, Forward]
```

That's 7 commands.  But the behavior **does not execute all 7 this tick**.  It does this:

```cpp
_navPlan.assign(plan.begin(), plan.end());   // store the whole plan
// ...
NavCmd next = _navPlan.front();              // take only the first command
_navPlan.pop_front();
executeNavCmd(next);                          // send TurnRight, wait for response
```

Next tick (after the server responds to TurnRight):
- Vision is stale → `refreshVision()` is called first.
- After vision arrives, the tick dispatcher runs the state handler again.
- The state handler sees `_navPlan` is non-empty (still has 6 commands).
- It pops `Forward`, executes it.

And so on.  **One server round-trip per command.**  The plan persists across ticks,
shrinking by one command each time, until it's empty.

This is the "execute one step, get a response, execute the next step" loop.

---

## 4. Why Vision Goes Stale After Every Move

After every `Forward` or turn, `setVisionStale()` is called in the response callback:

```cpp
_sender.expect("avance", [this](const ServerMessage& msg) {
    _commandInFlight = false;
    if (msg.isOk()) {
        // update dead-reckoned position
        setVisionStale();   // ← always
    } else {
        clearNavPlan();
        setVisionStale();
    }
});
```

This means at the top of the next tick, **before** the state handler runs, the behavior
will call `refreshVision()` and return.  Only after vision comes back does the state
handler run and pop the next command.

So the actual rhythm for a 3-step path is:

```
Tick 1:  execute Forward  → commandInFlight
Tick 2:  response arrives → visionStale=true
Tick 3:  refreshVision    → commandInFlight
Tick 4:  response arrives → visionStale=false
Tick 5:  execute Forward  → commandInFlight
Tick 6:  response arrives → visionStale=true
Tick 7:  refreshVision    → commandInFlight
Tick 8:  response arrives → visionStale=false
Tick 9:  execute Forward  → commandInFlight
...
```

Each physical step costs **two round-trips** to the server: one for the move command,
one for the vision refresh.  This is by design — the vision refresh lets the behavior
spot something better (food, a stone, a changed situation) and potentially abandon the
plan mid-way.

---

## 5. When Plans Are Kept vs Discarded

This is the crux of the confusion.  There are three situations:

### 5.1 Keep the plan: target is still valid, nothing changed

In `CollectStones`, the plan-or-replan logic is:

```cpp
std::string previousTarget = _navTarget;        // remember what we were going to
auto tile = getNearestTileWithNeededResource();  // re-evaluate from fresh vision

if (tile.localX == std::numeric_limits<int>::max()) {
    // No stone visible — explore
    if (_navPlan.empty()) {
        auto plan = Navigator::explorationStep(_explorationStep);
        _navPlan.assign(plan.begin(), plan.end());
    }
    // else: keep whatever exploration step was already planned
} else if (_navPlan.empty() || _navTarget != previousTarget) {
    // Stone visible AND (no plan yet OR the target changed) → rebuild
    auto plan = Navigator::planPath(_state.player.orientation, tile.localX, tile.localY);
    _navPlan.assign(plan.begin(), plan.end());
}
// else: _navPlan is non-empty AND target hasn't changed → keep it
```

The key condition is `_navPlan.empty() || _navTarget != previousTarget`.  If the plan
is not empty AND the target is the same stone type, the existing plan is kept.  This
avoids rebuilding on every tick for a multi-step journey.

The implicit assumption: **the plan is still valid if the target type didn't change**.
The plan was built from the tile's local coordinates at planning time, and those
coordinates were correct for the orientation at that moment.  Subsequent turns in the
plan already account for re-orienting, so the remaining steps should still be correct.

### 5.2 Discard and rebuild: target changed or plan exhausted

Plans are rebuilt when:
- `_navPlan` is empty (exhausted, or was never started).
- `_navTarget != previousTarget` (a different stone became closest, or food was spotted
  mid-stone-collection).
- The target disappeared from vision (checked in `refreshVision`'s callback):
  ```cpp
  if (!_navPlan.empty() && !_navTarget.empty() &&
      !_state.visionHasItem(_navTarget)) {
      clearNavPlan();   // target is gone, abandon the route
  }
  ```
- The move command failed (server returned `ko`):
  ```cpp
  } else {
      clearNavPlan();   // couldn't move, start fresh
      setVisionStale();
  }
  ```

### 5.3 Discard immediately: entering a new state or picking something up

Whenever the behavior transitions to a new state, or picks up an item on the current
tile (which removes the need to navigate to it), `clearNavPlan()` is called:

```cpp
// Entering CollectStones from CollectFood:
_aiState = AIState::CollectStones;
clearNavPlan();   // the food nav plan is irrelevant now

// Food on current tile:
if (_state.countItemOnCurrentTile("nourriture")) {
    clearNavPlan();   // no need to navigate anywhere
    _sender.sendPrend("nourriture");
    ...
}
```

The rule here is simple: **a plan is only valid for the state that created it**.  When
the state changes, the old plan is garbage.

---

## 6. The Role of `_navTarget`

`_navTarget` is a string label ("nourriture", "linemate", "deraumere", etc.) that
records **what the current plan is navigating toward**.  It serves two purposes:

**Purpose 1: Change detection.**  
Before rebuilding a plan, `CollectStones` saves the old target name, re-evaluates the
nearest needed resource, and compares:

```cpp
std::string previousTarget = _navTarget;
auto tile = getNearestTileWithNeededResource();  // may update _navTarget
// ...
} else if (_navPlan.empty() || _navTarget != previousTarget) {
    // rebuild
}
```

If the nearest stone type changed (say we were going for linemate but now sibur appeared
closer), `_navTarget` will differ from `previousTarget` and the plan gets rebuilt.

**Purpose 2: Vision-based invalidation.**  
In `refreshVision`'s callback, if the target is no longer visible in fresh vision:

```cpp
if (!_navPlan.empty() && !_navTarget.empty() &&
    !_state.visionHasItem(_navTarget)) {
    clearNavPlan();
}
```

Without `_navTarget`, you'd have no way to know whether the nav plan is still relevant
after a vision update.

`_navTarget` is cleared (`_navTarget.clear()`) whenever:
- A state transition happens and `clearNavPlan()` is called.
- An exploration step is used (there's no specific target to invalidate against).

---

## 7. The Two Navigation Modes

There are really two fundamentally different modes of navigation, and they use the
plan in completely different ways:

### Mode A: Target-directed navigation (`planPath`)

```
Know exactly where to go (localX, localY from vision)
→ Build full route upfront
→ Execute one step per tick
→ Keep the plan as long as target is the same and plan is non-empty
→ Rebuild only if target changes or plan runs out
```

This mode is used in `CollectFood` (going toward visible food) and `CollectStones`
(going toward a visible stone).

The plan can be multi-step — it's an ordered list of turns and forwards to reach a
specific tile.  The plan is computed *once* from the tile's local coordinates at the
moment of planning, then trusted to be correct until it either finishes or gets
invalidated.

### Mode B: Direction-directed navigation (`planApproachDirection` / `explorationStep`)

```
Don't know exactly where to go, only have a rough direction (or nothing)
→ Build a micro-plan of 1-2 commands (one step)
→ Execute it
→ Plan is immediately exhausted
→ On next tick, re-evaluate and build another micro-plan
```

This mode is used in `MovingToRally` (approaching a broadcast direction) and exploration
fallbacks (when nothing is visible).

The key difference: **the plan is intentionally short and always rebuilt from scratch**.
This is correct because:
- Exploration has no target, so there's nothing to preserve between steps.
- Broadcast direction changes with every RALLY message, so a multi-step plan in any
  fixed direction would quickly become wrong.

In Mode B, the "plan" is really just a convenience wrapper to keep `executeNavCmd` the
single dispatch point.  It's never meant to span more than 2 commands.

---

## 8. The Vision-Stale Loop and Navigation

Here's the full loop written out explicitly, so the rhythm is clear:

```
State handler wants to move → _navPlan is empty → call planXxx() → assign to _navPlan
→ pop front command → executeNavCmd(cmd) → send to server → _commandInFlight=true

[server responds]
→ callback fires → _commandInFlight=false → setVisionStale()

[next tick entry]
→ hasCommandInFlight()? NO
→ isVisionStale()? YES → refreshVision() → send "voir" → _commandInFlight=true

[server responds with vision]
→ callback fires → _commandInFlight=false → _staleVision=false
→ (possibly: target gone? clearNavPlan())

[next tick entry]
→ hasCommandInFlight()? NO
→ isVisionStale()? NO
→ isInventoryStale()? check...
→ dispatch to state handler
→ _navPlan non-empty? → pop front → executeNavCmd → repeat loop
```

Every single move has a `voir` injected after it.  This is the "one voir per step"
pattern.  It's intentional — it keeps the world model fresh — but it means navigation
is slow.  Each physical tile costs at minimum 2 server round-trips, often 3 or 4 if
inventory refresh also fires.

---

## 9. The `MovingToRally` Special Case

`MovingToRally` is the most confusing state because it uses Mode B navigation
(direction-directed, one step at a time) but it *looks* like it might want Mode A.

Here's why it can't use Mode A:

- `planPath` needs local `(x, y)` coordinates of the target tile — but you don't
  know which tile the leader is on.  You only know a compass quadrant.
- The correct tile to go to changes with every RALLY broadcast.  The leader's direction
  from you updates as both of you move.
- You might need to cross the map's wrap-around boundary, which `planPath` doesn't handle.

So the follower does this instead:

```
Receive RALLY:2 with direction=3 (right quadrant)
→ planApproachDirection(3, currentFacing) → [TurnRight, Forward]
→ _navPlan = [TurnRight, Forward]
→ execute TurnRight → wait → vision refresh
→ execute Forward → wait → vision refresh

Receive next RALLY:2 with direction=1 (straight ahead now — we're closer)
→ clearNavPlan()  ← MUST happen here (this is the bug from fix plan Step 1)
→ planApproachDirection(1, currentFacing) → [Forward]
→ execute Forward → wait → vision refresh

Receive next RALLY:2 with direction=0 (we're on the same tile)
→ transition to Rallying
```

The `clearNavPlan()` on direction update is critical here: without it, the TurnRight
and Forward from the previous step might still be sitting in `_navPlan` from a plan
built for a now-obsolete direction.  The follower would execute them anyway, walking
the wrong way.

This is the bug called out in Step 1 of the fix plan.

---

## 10. Summary: Decision Rules for Nav Plan Management

Here's the condensed decision table:

| Situation | Action |
|---|---|
| State transition to a new AIState | `clearNavPlan()` always |
| Item found on current tile | `clearNavPlan()` — no need to navigate |
| Target tile visible, no plan yet | Build full plan with `planPath` |
| Target tile visible, same target, plan non-empty | Keep existing plan |
| Target tile visible, target changed | Rebuild plan with `planPath` |
| Target tile gone from vision | `clearNavPlan()` (done in `refreshVision` callback) |
| No target visible | Use `explorationStep()` — 1-2 commands max, rebuild every step |
| Following a broadcast direction | Use `planApproachDirection()` — 1-2 commands, rebuild every RALLY update |
| Move command failed (`ko` from server) | `clearNavPlan()`, `setVisionStale()` |
| New RALLY direction received while in MovingToRally | `clearNavPlan()` — direction is stale |

**The master rule:** a nav plan is only valid for the state and target it was created
for, at the orientation it was created with.  Any change to any of those three things
is a signal to rebuild.

---

## 11. What a Correct Navigation Trace Looks Like

For a client facing North that needs to pick up a `linemate` at `localX=1, localY=2`
(one right, two forward):

```
[tick]  visionStale=true   → refreshVision (voir)
[tick]  vision arrives     → tile (1,2) has linemate → plan = [TurnRight, Forward, Forward, TurnLeft, Forward, Forward]
[tick]  CollectStones      → pop TurnRight → sendDroite
[tick]  droite ok          → facing=East, visionStale=true → refreshVision
[tick]  vision arrives     → plan non-empty, target=linemate → pop Forward → sendAvance
[tick]  avance ok          → x++ → visionStale=true → refreshVision
[tick]  vision arrives     → plan non-empty → pop Forward → sendAvance
[tick]  avance ok          → x++ → visionStale=true → refreshVision
[tick]  vision arrives     → plan non-empty → pop TurnLeft → sendGauche
[tick]  gauche ok          → facing=North → visionStale=true → refreshVision
[tick]  vision arrives     → plan non-empty → pop Forward → sendAvance
[tick]  avance ok          → y-- → visionStale=true → refreshVision
[tick]  vision arrives     → plan non-empty → pop Forward → sendAvance
[tick]  avance ok          → y-- → visionStale=true → refreshVision
[tick]  vision arrives     → plan EMPTY → linemate on current tile → clearNavPlan → sendPrend
[tick]  prend ok           → inventoryStale=true, visionStale=true
```

Six commands, twelve server round-trips, one collection.  This is normal and expected.

---

*End of document.*
