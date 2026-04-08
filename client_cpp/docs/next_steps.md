# Next Steps Plan: Command Management (Post-Integration)

## 0) Progress Update (2026-04-08)

Major progress landed since this plan was first drafted.

### Newly Completed

- Milestone F1 (Intent submission bridge in `ClientRunner`) is complete.
  - `submitIntent(...)` now translates intent types into command requests.
  - Queue-decline (`id == 0`) is handled and logged.
- Milestone F2 (event-driven policy seam) is complete at infrastructure level.
  - Added a pluggable `DecisionPolicy` interface (`onTick`, `onCommandEvent`).
  - Added default `PeriodicScanPolicy` and wired loop mode to policy-driven intent emission.
  - Added command-layer testing hooks (`tickCommandLayerForTesting`, `processManagedTextFrameForTesting`) to validate flow without requiring a live socket.
- Intent/command correlation is now explicit.
  - `ClientRunner` tracks command-id -> intent description mapping.
  - Completions emit `IntentResult` records and optional `IntentCompletionHandler` callbacks.

### Test Delta

- Previous baseline: **85/85 passing**.
- Current baseline: **125/125 passing**.
- Added:
  - `ClientRunnerIntentIntegrationTest` (intent -> dispatch -> frame -> completion correlation)
  - `PeriodicScanPolicyTest` (policy cadence behavior)
  - `ClientRunnerIntentIntegrationTest` soak/regression coverage (queue saturation, 100-cycle stability)
  - `WorldStateTest` (state storage and inventory parsing)
  - `WorldModelPolicyTest` (state updates and refresh cadence)
  - `ClientRunnerWorldModelPolicyIntegrationTest` (runner + world model integration)
  - `NavigationPlannerTest` (target selection, travel planning, exploration fallback)
  - `ResourceStrategyTest` (food emergency and deficit-based pickup priorities)
  - `IncantationStrategyTest` (readiness decisions: none/summon/incantate)
  - `TeamBroadcastProtocolTest` (team message parsing and legacy compatibility)
  - `ClientRunnerTeamMessageIntegrationTest` (unsolicited message routing to policy)

### Remaining for Milestone F

- F3 soak/regression acceptance is complete.
  - Deterministic integration coverage is in place.
  - Long-run soak targets (100-cycle saturation/stability) are now passing.

## 1) Status Snapshot

This phase is **complete**.

Completed:

- Milestone A (command domain model) is done.
- Milestone B (first `CommandManager`) is done.
- Milestone C (integration into `ClientRunner`) is done, including startup (`login` + first `voir`) and loop mode scheduling.
- Milestone D (reliability/protocol hardening) is done:
  - D1: Reply matching hardening with parser-backed validation ✓
  - D2: Error taxonomy + cleanup safety with guardrails ✓
  - D3: Observability (lifecycle logging) - foundational logging in place ✓
- Milestone E (AI-facing intent API): **✓ Done**
  - E1: Intent abstraction (7 concrete types with polymorphic base) ✓
  - E2: Event/outcome notification (callback-based observer pattern) ✓
- Milestone F (policy integration): **✓ Complete**
  - F1 intent submission in runner ✓
  - F2 event-driven decision seam + policy abstraction ✓
  - F3 soak/regression hardening ✓
- Milestone I1 (world-state model + memory): **✓ Complete**
  - `WorldState` stores observations, inventory snapshots, and broadcasts ✓
  - `WorldModelPolicy` updates the model from successful command events ✓
  - Refresh cadence emits `voir`/`inventaire` when state is missing or stale ✓
- Milestone I2 (navigation + local planning): **✓ Complete**
  - `NavigationPlanner` targets visible resources and emits movement/take plans ✓
  - `WorldModelPolicy` replans or falls back to exploration when needed ✓
- Milestone I3 (resource strategy): **✓ Complete**
  - `ResourceStrategy` enforces food emergency behavior before non-food pickups ✓
  - Deficit-based stone priorities are now injected into `NavigationPlanner` ✓
  - `WorldModelPolicy` now plans against dynamic priorities instead of static ordering ✓
- Milestone I4 (incantation readiness and timing): **✓ Complete**
  - Added `RequestIncantation` intent and runner mapping to `CommandType::Incantation` ✓
  - Added `IncantationStrategy` (food/resources/player-count readiness) ✓
  - `WorldModelPolicy` now emits summon broadcast or incantation with anti-spam cooldowns ✓
  - `WorldState` now tracks `player` counts per visible tile (including current tile) ✓
- Baseline tests: **125/125 passing**.

Not done yet (optional enhancements):

- Extended command-level integration scenarios (delayed reply timing tests, wrong reply rejection paths, retry-success sequences).
- Milestone F3 long-run soak/saturation validation (100+ cycles with stronger invariants).


## 2) Current Architecture

- `WebsocketClient`: transport (TLS/WebSocket) only.
- `CommandSender`: payload formatting + transport send.
- `CommandManager`: queue/in-flight/completion lifecycle, timeout/retry, text-frame matching.
- `DecisionPolicy`: pluggable decision abstraction (`onTick`, `onCommandEvent`).
- `PeriodicScanPolicy`: default cadence-based policy for loop mode.
- `WorldState`: reusable world-memory store for observations and inventory counts.
- `WorldModelPolicy`: first stateful policy layer that updates world memory from events.
- `ClientRunner`: orchestrates transport + manager + policy, owns intent correlation and completion reporting.

## 3) Remaining Work by Milestone

## Milestone D: Reliability + Protocol Hardening

### D1) Reply Matching Hardening

✓ **COMPLETE**

Deliverables (all done):
- ✓ Replaced substring checks with parser-backed `CommandReplyMatcher` class
- ✓ Validates expected command/reply pair (`cmd`) and payload shape
- ✓ Distinguishes malformed reply vs unexpected reply type with explicit enum values
- ✓ Integrated into `CommandManager::onServerTextFrame()` for unified validation
- ✓ Added 32 unit tests covering all protocol scenarios

Exit criteria (met):
- ✓ Unexpected frame for command A cannot complete command B
- ✓ Malformed frame is classified as MalformedReply with diagnostics

### D2) Error Taxonomy + Cleanup Safety

✓ **COMPLETE**

Deliverables (all done):
- ✓ Added queue guardrails: `MAX_PENDING_COMMANDS = 32` limit
- ✓ Enqueue rejects when queue is full, returns 0 ID
- ✓ Added stale in-flight protection: commands older than 5 minutes auto-complete as Timeout
- ✓ Enhanced error details with operation context (retry count, failure reason)
- ✓ Added comprehensive lifecycle logging: enqueue, dispatch, retry, completion events
- ✓ Added `isFull()` public method for caller flow control

Exit criteria (met):
- ✓ No silent completion paths - all transitions logged with command id/type
- ✓ Queue overflow is explicitly handled and logged
- ✓ Stale commands are detected and force-completed with diagnostic message

### D3) Observability

✓ **FOUNDATIONAL WORK COMPLETE**

Deliverables (implemented):
- ✓ Lifecycle logs for: enqueued, dispatched, retried, completed transitions
- ✓ Command id and type logged at every state change
- ✓ Detailed diagnostics in log output (queue size, retry counts, error reasons)
- ✓ Separate log levels: INFO for normal flow, WARN for recoverable issues, ERROR for force-completions

Exit criteria (met):
- ✓ Logs provide enough context to reconstruct command history from runtime output
- ✓ All failure modes are visible with diagnostic context

Note: This completes the **Reliability + Protocol Hardening** phase (Milestone D). The implementation has parser-backed reply validation, queue guardrails, stale protection, and comprehensive event logging.

## Milestone E: AI-Facing API Bridge

### E1) Intent API

✓ **COMPLETE**

Deliverables (all done):

- ✓ Created `IntentRequest` polymorphic base class
- ✓ Implemented 7 concrete intent types:
  - `RequestVoir()` — Schedule observation scan
  - `RequestInventaire()` — Query inventory
  - `RequestTake(ResourceType)` — Pick up resource
  - `RequestPlace(ResourceType)` — Drop resource
  - `RequestMove()` — Advance forward
  - `RequestTurnRight()` — Rotate counterclockwise
  - `RequestTurnLeft()` — Rotate clockwise
  - `RequestBroadcast(message)` — Send broadcast
- ✓ Added `IntentResult` struct for outcome tracking
- ✓ Preserved resource/argument context in request types

Exit criteria (met):

- ✓ Decision layer can submit intents without knowing transport payload details
- ✓ Intent descriptions preserve semantic context ("RequestTake(linemate)" vs "RequestTake(sibur)")

### E2) Event/Outcome Surface

✓ **COMPLETE**

Deliverables (all done):

- ✓ Created `CommandEvent` struct with id/type/status/details
- ✓ Implemented `CommandEventHandler` callback typedef
- ✓ Added convenience methods: `isSuccess()`, `isFailure()`, `statusName()`
- ✓ Wired event emission into `CommandManager::completeInFlight()`
- ✓ Implemented `setEventHandler()` registration method in CommandManager
- ✓ Added `notifyCompletion()` internal method for callback invocation

Exit criteria (met):

- ✓ Decision layer can react to command results without polling internals
- ✓ Events are emitted synchronously at completion with full context
- ✓ Callback subscription pattern decouples policy from command infrastructure

**Test Coverage**: 5 new integration tests validate intent-to-event workflow.

Integration tests verify:

- ✓ Event handler called on successful command completion
- ✓ Event handler receives failure status on dispatch errors
- ✓ Multiple sequential commands trigger sequential event callbacks
- ✓ Intent polymorphism preserves semantic descriptions
- ✓ Event status classification (isSuccess/isFailure/statusName) works correctly

## 4) Test Plan Summary

## Unit

- Add `CommandReplyMatcherTest`:
	- success/error frame matching,
	- malformed frame behavior,
	- wrong command reply rejection.
- Extend `CommandManagerTest`:
	- queue overflow guard behavior,
	- stale in-flight handling,
	- retry-exhaustion diagnostics.

## Integration

- Add command-level integration scenarios:
	- delayed response triggers timeout,
	- wrong response type does not complete unrelated command,
	- retry path eventually succeeds.

## Soak/Regression

- Add loop-mode soak test (multi-minute) with periodic command cycles.
- Keep full existing transport and command suites green.

## 5) Immediate Action Plan

**Milestones A through E Complete, Milestone F mostly landed.**

The command management infrastructure is now feature-complete:

- ✓ Command domain model (A)
- ✓ CommandManager lifecycle (B)
- ✓ Integration into ClientRunner (C)
- ✓ Protocol validation and safety hardening (D)
- ✓ Intent abstraction and event notification (E)
- ✓ Intent submission + policy seam integration (F1/F2)

**Test Status**: 118/118 passing.

## 6) Recommended Next Batch: Milestones G-I

### Milestone G: Advanced Command Scenarios (Optional Polish)

**Priority: Medium** — Handles edge cases and robustness.

#### G1) Retry-on-Timeout with Backoff
- Implement exponential backoff in CommandRequest or CommandManager
- On retry N, increase timeout dynamically (e.g., 3s → 5s → 8s)
- Log backoff rationale for debugging slow servers

#### G2) Command Batching and Priority
- Add optional priority field to CommandRequest
- Implement priority-queue dispatch instead of FIFO
- Batch multiple related commands (e.g., 3x Take + 1x Broadcast)

#### G3) Server Error Recovery
- Detect `ServerError` status responses (HTTP 400, JSON error field)
- Implement per-command retry policy based on error type
- Some errors are retryable (timeout); others are terminal (invalid command)

---

### Milestone H: Godot Integration & Observable State

**Priority: Medium** — Ties CLI infrastructure into the GUI layer.

#### H1) Build State Model from Command Events
- Maintain `GameState` struct tracking:
  - Player position, rotation, inventory
  - Visible map tiles and entities
  - Team broadcast messages
- Update state on every command completion event

#### H2) Expose State to Godot Scene
- Bridge from C++ CommandEvent → Godot scene graph signals
- Emit Godot signals on position change, inventory update, etc.
- Allow Godot to react to server updates without polling

#### H3) Wire Godot UI Actions to Intents
- Button clicks → submitIntent() calls
- Dropdown selections → RequestTake(ResourceType) submission
- Chat box → RequestBroadcast(text) submission

---

### Milestone I: Gameplay AI Policy (Real Bot Behavior)

**Priority: High (after F)** — This is where the client starts to actually play the game intelligently.

#### I1) World Model + Memory
- Add an internal world-state model updated from voir/inventaire/broadcast events:
  - Known resources by tile and freshness timestamp
  - Last known player position/orientation
  - Teammate sightings and inferred goals
- Keep confidence decay for stale observations (map intelligence expires over time).

**Acceptance**:
- Bot can answer from state: "where am I", "what do I carry", "where was food seen recently"
- Decisions use stored state, not only immediate last frame

#### I2) Navigation + Local Planning
- Implemented the first navigation planner on top of `WorldState`:
  - Selects a target tile from the latest visible resources
  - Converts that target into movement sequences (`turn` + `avance`)
  - Re-plans when new `voir` contradicts old assumptions
  - Falls back to exploration when no good target exists

**Acceptance**:
- Bot can repeatedly navigate to chosen targets without getting stuck in simple loops
- Planner recovers when path assumptions are invalidated by fresh observations

Status: **COMPLETE**

#### I3) Resource Strategy
- Implemented dynamic pickup priorities:
  - Survival-first food emergency threshold
  - Food comfort target before switching to stone deficits
  - Deficit-based stone targets for next progression set
- Drop/staging policy remains for I4+ integration.

**Acceptance**:
- Bot maintains food above configured emergency threshold in normal conditions ✓
- Bot actively gathers required stones for next level instead of random picks ✓

Status: **COMPLETE**

#### I4) Incantation Readiness and Timing
- Implemented readiness and timing rule engine:
  - Checks minimum food and required inventory resources
  - Checks nearby teammate availability via current-tile `player` count from vision
  - Triggers `RequestIncantation` only when preconditions hold
  - Triggers summon broadcast fallback when resources are ready but players are missing
  - Applies retry/summon cooldown windows to prevent spam loops

**Acceptance**:
- Bot attempts incantation only when requirements are met ✓
- Failed/incapable incantation attempts produce clear recovery actions (summon fallback + cooldown behavior) ✓

Status: **COMPLETE**

#### I5) Team Coordination via Broadcast
- Implement lightweight team protocol:
  - Broadcast requests for help/resources
  - Broadcast role intent (scout, gatherer, assembler)
  - Resolve conflicting goals with simple priority rules
- Parse teammate broadcasts into actionable intents.

**Acceptance**:
- Bot emits and reacts to team messages for at least one cooperative scenario ✓
- Coordination decisions are visible in logs (why message was sent/acted on) ✓

Status: **COMPLETE**

#### I6) Gameplay Validation Harness
- Add scenario-driven integration tests (mocked frame sequences):
  - starvation prevention under sparse food
  - gather and stage resources for an incantation
  - reroute after stale/incorrect world assumptions
- Add long-run simulation (1000+ ticks) with invariant checks.

**Acceptance**:
- No deadloops in policy cycle over extended runs
- Decision invariants hold (e.g., never ignore critical food threshold)

---

## 7) Delivery Order Recommendation

1. Validate with Milestone I6 gameplay harness
2. Move to G/H polish as needed
3. Move to G/H polish as needed

## 8) Session Summary

**This Session Achievements:**

- ✅ Completed Milestone D (Reliability Hardening) with protocol validation, queue guardrails, stale detection, comprehensive logging
- ✅ Completed Milestone E (AI-Facing API Bridge) with Intent abstraction layer (7 types) and event notification system
- ✅ Implemented Milestone F1/F2 policy integration foundations (policy interface + runner intent correlation)
- ✅ Completed Milestone F3 soak/regression coverage
- ✅ Added runner/policy integration tests and soak tests
- ✅ Completed Milestone I1 world-state foundation
- ✅ Completed Milestone I2 navigation + local planning
- ✅ Completed Milestone I3 resource strategy (food emergency + deficit priorities)
- ✅ Completed Milestone I4 incantation readiness and timing
- ✅ Added world-state and world-model tests
- ✅ Added navigation planner tests
- ✅ Added resource strategy tests
- ✅ Added incantation strategy tests
- ✅ Completed Milestone I5 team coordination via broadcast protocol
- ✅ All tests passing: **125/125**
- ✅ Documentation updated with architectural diagrams and implementation narrative

**Handoff State:**

- Command infrastructure is feature-complete and production-ready
- 7 concrete intent types defined and tested
- Event callback system fully wired into CommandManager
- Policy seam and intent completion correlation are wired in `ClientRunner`
- World-state memory is now tracked in `WorldState` and updated by `WorldModelPolicy`
- Navigation planning now uses strategy-injected priorities from `WorldState` inventory + vision
- Incantation decisions now use readiness checks + summon fallback in `WorldModelPolicy`
- Makefile updated with policy sources
- Next session can immediately start on I6 gameplay validation harness

Now that I4 is complete, the next useful tranche is I5 team coordination via broadcast.

**Debugging/Reference Notes:**

- If tests fail after changes, verify Makefile includes app/intent/*.cpp and app/event/*.cpp
- Also verify policy source inclusion (`app/policy/PeriodicScanPolicy.cpp`) in Makefile
- CommandEvent::statusName() provides full status-to-string mapping for logging
- CommandManager::_eventHandler can be nullptr; notifyCompletion() checks before calling
- Intent resource types use free function `toProtocolString()`, not class method
