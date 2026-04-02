# Next Phase Plan: Command Management

## 1) Goal of this phase

Move from direct command sends in `ClientRunner` to a real command-management layer that:

- decides what to send and when,
- tracks in-flight commands and expected replies,
- handles timeouts/retries/failures cleanly,
- exposes a stable API for the future AI decision layer.

Transport is already validated (unit + integration), so this phase focuses on orchestration and protocol behavior above transport.

## 2) Current baseline (already in code)

- `CommandSender` can serialize/send: login, voir, inventaire, prend nourriture.
- `ClientRunner` currently owns command cadence and response handling logic.
- Periodic behavior exists (`voir`, `inventaire`, low-food pickup) but is tightly coupled to runner flow.
- No explicit in-flight registry, command IDs, timeout policy, or retry policy yet.

## 3) Target architecture for this phase

Keep responsibilities explicit:

- `WebsocketClient`: transport only (already done).
- `CommandSender`: payload formatting + enqueue to websocket.
- `CommandManager` (new): command lifecycle and policy.
- `ClientRunner`: high-level app loop, delegates command orchestration to `CommandManager`.

### Proposed `CommandManager` responsibilities

- Maintain command queue with priorities.
- Maintain at-most-one or bounded in-flight command set (start simple: one in-flight).
- Attach metadata per command:
	- command type,
	- enqueue timestamp,
	- timeout deadline,
	- retry count,
	- expected reply matcher.
- Accept server frames and resolve/reject matching in-flight commands.
- Emit typed outcomes for upper layers (success, timeout, protocol-error, server-error).
- Apply backoff/retry policy for transient failures.

## 4) Execution plan by milestones

## Milestone A: Command domain model

Deliverables:

- Add command metadata types:
	- `CommandType` enum,
	- `CommandSpec` (timeoutMs, maxRetries, expectsReply),
	- `CommandRequest`,
	- `CommandStatus`/`CommandResult`.
- Add reply-matching helpers (start with robust JSON substring matching, later parser-backed).

Exit criteria:

- Unit tests cover command construction and timeout computation.
- No behavior change in runtime path yet.

## Milestone B: First `CommandManager` implementation

Deliverables:

- Implement `CommandManager` with APIs similar to:
	- `enqueue(CommandRequest)`,
	- `tick(int64_t nowMs)`,
	- `onTextFrame(const std::string&)`,
	- `hasPendingWork()`.
- Start with single in-flight command to reduce complexity.
- Support core commands:
	- login,
	- voir,
	- inventaire,
	- prend nourriture.

Exit criteria:

- Unit tests verify queue -> send -> in-flight -> completion.
- Timeout transitions are deterministic.
- Retry behavior respects max retries.

## Milestone C: Integrate into `ClientRunner`

Deliverables:

- Move periodic scheduling logic from `ClientRunner` to manager-driven scheduling hooks.
- `ClientRunner` forwards incoming frames to manager instead of directly parsing each command response path.
- Preserve existing observable behavior in loop mode.

Exit criteria:

- Existing transport flow still works.
- Existing command cadence is preserved or intentionally improved.
- No regressions in connection stability.

## Milestone D: Reliability and protocol hardening

Deliverables:

- Add explicit error classes for:
	- timeout,
	- malformed reply,
	- unexpected reply type,
	- server error payload.
- Add dead-command cleanup and stale in-flight protection.
- Add guardrails to prevent queue flooding.

Exit criteria:

- Soak-style loop test for several minutes without command-state leaks.
- Clear log messages for every command lifecycle transition.

## Milestone E: AI-facing API (bridge to next phase)

Deliverables:

- Expose a clean API for decision layer:
	- submit intent (`RequestVoir`, `RequestTake("nourriture")`, etc.),
	- receive command outcomes/events.
- Keep strategy logic outside manager; manager remains deterministic infra.

Exit criteria:

- Decision layer can trigger commands without touching transport details.

## 5) Test plan for this phase

## Unit tests

- `CommandManagerTest`:
	- enqueue order and priority behavior,
	- one in-flight policy,
	- timeout transitions,
	- retry exhaustion,
	- unexpected reply handling.
- `CommandReplyMatcherTest`:
	- success/error frame matching,
	- malformed frame behavior.

## Integration tests

- Extend TLS/WebSocket loopback test setup to include command-level scenarios:
	- successful login -> voir round trip,
	- delayed response triggers timeout,
	- wrong response type does not complete unrelated command,
	- retry eventually succeeds.

## Regression tests

- Keep existing transport tests untouched and green.
- Add at least one end-to-end loop-mode test with multiple command cycles.

## 6) Suggested implementation order (PR-friendly)

1. Add command domain types + unit tests (no runtime wiring).
2. Add `CommandManager` core + unit tests.
3. Wire into `ClientRunner` behind a small compatibility layer.
4. Add integration command scenarios using existing loopback server test pattern.
5. Add AI-facing API surface and prepare handoff to strategy/decision phase.

## 7) Definition of done for command-management phase

- Command lifecycle is explicit and test-covered.
- `ClientRunner` no longer owns low-level command orchestration rules.
- Failures are classified (timeout/protocol/server/network) with actionable logs.
- Existing transport suite remains green.
- New command-manager unit/integration suites are green.
- API is ready for the next phase: command selection strategy / behavior tree / planner.

## 8) Immediate next action

Start with Milestone A by creating command domain types and tests before moving runtime logic.
