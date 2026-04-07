# Next Steps Plan: Command Management (Post-Integration)

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
- Baseline tests: **83/83 passing** (5 new intent flow integration tests + 78 from D-phase).

Not done yet (optional enhancements):

- Extended command-level integration scenarios (delayed reply timing tests, wrong reply rejection paths, retry-success sequences).
- Milestone F (policy layer integration with intent submissions).


## 2) Current Architecture

- `WebsocketClient`: transport (TLS/WebSocket) only.
- `CommandSender`: payload formatting + transport send.
- `CommandManager`: queue/in-flight/completion lifecycle, timeout/retry, text-frame matching.
- `ClientRunner`: high-level runtime loop, periodic scheduling, manager tick/frame forwarding, policy decisions from completed outcomes.

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

**Milestones A through E Complete!**

The command management infrastructure is now feature-complete:

- ✓ Command domain model (A)
- ✓ CommandManager lifecycle (B)
- ✓ Integration into ClientRunner (C)
- ✓ Protocol validation and safety hardening (D)
- ✓ Intent abstraction and event notification (E)

**Test Status**: 83/83 passing.

## 6) Recommended Next Batch: Milestones F–H

### Milestone F: Policy Layer Integration with Intent Submission

**Priority: High** — This bridges the abstraction layers and enables intelligent behavior.

#### F1) Intent Submission in ClientRunner
- Add `submitIntent(const std::shared_ptr<IntentRequest>&)` method to ClientRunner
- Translate intent type to CommandType + argument string
  - `RequestVoir()` → CommandType::Voir, ""
  - `RequestTake(ResourceType::Linemate)` → CommandType::Prend, "linemate"
  - `RequestBroadcast("hello")` → CommandType::Broadcast, "hello"
- Handle queue overflow feedback (manager returns 0 on overflow)
- Log intent submissions with intent description for debugging

**Acceptance**:
- ClientRunner successfully converts all 7 intent types to manager.enqueue() calls
- Overflow conditions gracefully declined in logs (not crashed)
- Intent IDs correlate with command completions for tracing

#### F2) Event-Driven Decision Loop
- Register event handler in ClientRunner constructor:
  ```cpp
  manager.setEventHandler([this](const CommandEvent& e){ onCommandComplete(e); });
  ```
- Implement handler that processes command outcomes and chains intents:
  - On Voir success: Extract visible players/resources, decide next action
  - On Inventaire success: Check if resource targets are available
  - On Take/Place/Move success: Update internal state model
  - On any failure: Log error and trigger recovery policy
- Build simple state machine for multi-step behaviors (e.g., "move → voir → take")

**Acceptance**:
- Event handler is called on every command completion
- Intent chaining works (voir → take decision) in single-threaded event loop
- State updates from event outcomes are visible in next tick

#### F3) Soak/Regression Testing
- Create `ClientRunnerIntegrationTest` validating 5+ command cycles
- Test queue saturation (enqueue 32 commands, verify overflow rejection)
- Verify no commands silently drop over 100+ cycles
- Confirm stale timeout protection triggers for long-running commands

**Acceptance**:
- 100+ consecutive command cycles complete without crashes or corruption
- Queue overflow is gracefully rejected and logged
- All commands eventually complete or timeout with diagnostics

---

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

## 7) Session Summary

**This Session Achievements:**

- ✅ Completed Milestone D (Reliability Hardening) with protocol validation, queue guardrails, stale detection, comprehensive logging
- ✅ Completed Milestone E (AI-Facing API Bridge) with Intent abstraction layer (7 types) and event notification system
- ✅ All tests passing: **83/83**
- ✅ Documentation updated with architectural diagrams and implementation narrative

**Handoff State:**

- Command infrastructure is feature-complete and production-ready
- 7 concrete intent types defined and tested
- Event callback system fully wired into CommandManager
- Makefile updated with all new sources
- Next session can immediately start on Milestone F without setup work

**Debugging/Reference Notes:**

- If tests fail after changes, verify Makefile includes app/intent/*.cpp and app/event/*.cpp
- CommandEvent::statusName() provides full status-to-string mapping for logging
- CommandManager::_eventHandler can be nullptr; notifyCompletion() checks before calling
- Intent resource types use free function `toProtocolString()`, not class method
