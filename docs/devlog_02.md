# Zappy Client - Devlog - 2

## Table of Contents
1. [Command Center](#21---command-center)
2. [I Want to Talk to the Commander!](#22-i-want-to-talk-to-the-commander)
3. [Commander Takes the Wheel](#23---commander-takes-the-wheel)
4. [Hardening the Protocol](#24---hardening-the-protocol)
5. [AI-Facing API Bridge](#25---ai-facing-api-bridge)


<br>
<br>

# 2.1 - Command Center
Today we're going to focus on building what we'll call a **Command Domain**, through which we'll start managing every command-based communication between the client and the server, going from the hardcoded behaviour currently placed in the `ClientRunner` towards a proper communication loop based on pre-formed data. In order to do so, the first thing we'll need is some struccts to manage different command-related aspects, like `Type`, `Status`, `Specs`, `Results` and `Requests`. Most of these are going to be bundled in the `Request`, which will be coupled with the `Result` and the `Status`. So, for the base command data structs, we'll have:
```cpp
enum class CommandType {
    Unknown = 0,
    Login,
    Avance,
    Droite,
    Gauche,
    Voir,
    Inventaire,
    Prend,
    Pose,
    Expulse,
    Broadcast,
    Incantation,
    Fork,
    ConnectNbr
};
```
```cpp
struct CommandSpec {
	int		timeoutMs = 3000;
	int		maxRetries = 0;
	bool	expectsReply = true;

	static CommandSpec forType(CommandType type) {
		switch (type) {
			case CommandType::Login: return {4000, 1, true};
			case CommandType::Avance: return {4000, 1, true};
			case CommandType::Droite: return {4000, 1, true};
			case CommandType::Gauche: return {4000, 1, true};
			case CommandType::Voir: return {8000, 1, true};
			case CommandType::Inventaire: return {4000, 1, true};
			case CommandType::Prend: return {4000, 2, true};
			case CommandType::Pose: return {4000, 1, true};
			case CommandType::Expulse: return {5000, 1, true};
			case CommandType::Broadcast: return {4000, 1, true};
			case CommandType::Incantation: return {120000, 0, true};
			case CommandType::Fork: return {10000, 1, true};
			case CommandType::ConnectNbr: return {2000, 1, true};
			default: return {3000, 0, true};
		}
	}
};
```
```cpp
enum class CommandStatus {
	Success = 0,
	Timeout,
	ProtocolError,
	ServerError,
	NetworkError,
	Retrying
};
```
```cpp
enum class ResourceType {
	Nourriture = 0,
	Linemate,
	Deraumere,
	Sibur,
	Mendiane,
	Phiras,
	Thystame
};
```

And for the `Request` and the `Result` we'll have:
```cpp
struct CommandRequest {
	std::uint64_t	id = 0;
	CommandType		type = CommandType::Unknown;
	std::string		arg;
	CommandSpec		spec{};
	std::int64_t	enqueuedAtMs = 0;
	std::int64_t	deadlineAtMs = 0;
	int				retryCount = 0;

	static CommandRequest make(std::uint64_t id, CommandType type, std::int64_t nowMs, const std::string& arg = "") {
		CommandRequest req;
		req.id = id;
		req.type = type;
		req.arg = arg;
		req.spec = CommandSpec::forType(type);
		req.enqueuedAtMs = nowMs;
		req.deadlineAtMs = nowMs + req.spec.timeoutMs;
		return req;
	}
};
```
```cpp
struct CommandResult {
	std::uint64_t	id = 0;
	CommandType		type = CommandType::Unknown;
	CommandStatus	status = CommandStatus::Success;
	std::string		details;
};
```

As you can see, and you might have already concluded yourself at this point, the process of building corners of this program follows through the same questions/steps when kickstarting their process:
- What objects/concepts/responsibilities are embeded in this part of the program?
- What kind of data does this piece of the program need to manage? What does its *concept* entrail?
- How is the raw data going to be passed around by functions and interfaced by the user?

In our case, things are *kind of* straight forward (if you know what you're doing in regards to how a *Zappy* playing client needs to behave, which I realize is mostly not going to be the case becase, well, that's what we're trying to figure out *right now*), at least in the conceptual level issue of *what do we need to store, track, pass, etc.*. We know that *Zappy* is a game based around an array of different commands, so we need a `CommandType` enmerator, and because we know that some commands have specific targets, like `prend`, we're going to need an enumerator for what is being *taken* or *placed* by a player *from* or *to* a tile, the `ResourceType`. We also know that, in general, a command communication between client and server can have multiple results, it can succeed, fail, be wrongly managed,... So, what to write? Of course, another enum, this time a `CommandStatus`. And following task requirements, we know that the required times for each command before timeout, to which we're adding some retry possibility amounts, all to build a `CommandSpec` to store information about specificities about each commands.

Now, the `Request` and `Result` parts are a little, tiny bit more complicated, but almost not at all. `Result`, agains as per the tasks requirements, needs to store an internal `id` and a `CommandType` for tracking, as well as a status (we need to know if the command was successful or any other possible outcome) and its details, which in general are going to be just an `ok`/`ko` response, but in some cases is going to be a whole info string/JSON (for example: `voir`, `inventory`, `incantation`, all need to handle a complex return). `Request`, the core of this pre-building stage, is the bulkiest struct, but it really is still just a bunch of bundled data an a pretty basic static function for interfacing. Besides command related data, it needs to track the times at which the request is done and at which it should die if unhandled, and a `make()` method that builds a specific `CommandRequest` and returns it.

All of this information is known to us, so really what we needed to do here is to sit down and think about how all of this needs and specifications could be translated into manageable data structs (or classes, if you'd rather). And because this ends up giving us finite, concrete items, we can test them with a new suite, checking if the web of new structs are correctly understanding each other (which they are, as proven by the [Command Domain Tests](../client_cpp/tests/unit/CommandDomainTest.cpp)!). 

<br>
<br>

# 2.2 I Want to Talk to the Commander!
*Because we're now going to focus on the Manager, get it????* With the data foundation in place, we need to build the proper command managing class, which is basically a small orchestration layer between the game logic and the transport. Keeping ourselves inside our aims to build independent modules, there's no need for the `CommandManager` to speak WebSocket directly, it just needs to receive a dispatch callback and decide when to send, when to wait, when to retry, and when to mark a command as done. And to do so, we're going to need several internal attributes:
- `pending`: for queued commands waiting to be sent
- `inFlight`: for the single command currently waiting on a response
- `completed`: for the finished command results waiting to be consumed by the caller
- `dispatch`: a callback that actually sends one command through the transport
- `nextId`: a monotonically increasing ID for each enqueued command

After deciding on these, what we need is to figure out how the **command lifecycle** needs to look like. We know that a caller will enqueue a command to create a `CommandRequest`, which will get an ID and timing data, and will be pushed to the `pending` queue. We also know that we're going to have to periodically check if the manager has either an `inFlight` request or a waiting queue, for which we'll need a `tick()` function, synchronized with the general client's ticking, that folows suit, starting new command requests when necessary (i.e., moving the head of `pending` to `inFlight` and calling the `dispatch` function; but also handling the timeout/retry logic if the `inFlight` expires, based on the tick time). Furthermore, when a server text frame arrives, the caller is going to need to pass it to `onServerTextFrame()`, and work arund different possibilities: if it matches the current `inFlight` command, the manager needs to complete it; if the frame contains an `error` or says `ko`, the manager needs to complete it as a `ServerError`; and if its unrelated, the manager has to ignore it and return an aknowledgement of the ticking (a `false` will suffice). And finally, because the completed commands are stored (`completed`), the caller can retrieve any successfull command from there via some pop function so that decisions can be made. All in all, the class header is going to look something like this (having in mind that the timing of the commands is handled via the `CommandSpec`):
```cpp
class CommandManager {
	using DispatchFn = std::function<Result(const CommandRequest&)>;
	
	private:
		DispatchFn						_dispatch;
		std::deque<CommandRequest>		_pending;
		std::optional<CommandRequest>	_inFlight;
		std::deque<CommandResult>		_completed;
		std::uint64_t					_nextId;

	private:
		Result	dispatchInFlight();
		void	startNextIfIdle();
		void	completeInFlight(CommandStatus status, const std::string& details);

		bool	isServerErrorReply(const std::string& text) const;
		bool	isCommandKoReply(const std::string& text) const;
		bool	matchesInFlightReply(const CommandRequest& req, const std::string& text) const;

	public:

		explicit CommandManager(DispatchFn dispatch);

		std::uint64_t	enqueue(CommandType type, std::int64_t nowMs, const std::string& arg = "");
		Result			tick(std::int64_t nowMs);
		bool			onServerTextFrame(const std::string& text);

		bool			hasInFlight() const;
		std::size_t		queuedCount() const;
		bool			popCompleted(CommandResult& out);
};
```

This first version is surely too simple, but manageable (heh) at this stage. We'll start with only one command in flight at a time, with string-based matching and without driving the `ClientRunner`. We'll take care of those just after testing this initial prototype, because for now what we need to lock ourselves in is the command management flow:
1. `enqueue()` is ging to create and enqueue a `CommandRequest`
2. `tick()` is going to start next queued request if iddle, retry (if allowed) or complete with `Timeout()` if the `inFlight` dies, go through `NetworkError` if dispatch fails, refresh the flight timing if a retry succeeds with a new deadline.
3. `onServerTextFrame()` will handle the login (auto success for now), match the string results for voir/inventaire/prend, and complete as `ServerError` if `error`/`ko`
4. `popCompleted()` will return the oldest finished command result

One design choice around the manager, as previously mentioned, is to make it transport-agnostic, i.e., it doesn't nor *needs to* know what's happening in the transport layer. It just decides policy and handles lifecycle, while the transport actually sends the bytes and returns the `Result`. This meant that, at this point in the story, this was still an isolated prototype. The next step was to actually wire it into the runtime path so that the `ClientRunner` stopped doing direct command orchestration itself.

<br>
<br>

# 2.3 - Commander Takes the Wheel
The final steps of this iteration were all about integration and behavior-preserving migration. The objective was to keep the same observable client behavior, but move command ownership from ad-hoc runner code to the manager lifecycle.

The runtime path now works like this:

1. The `ClientRunner` owns a `CommandManager` instance configured with a dispatch callback.
2. That callback maps `CommandType` to sender actions (`login`, `voir`, `inventaire`, `prend <resource>`).
3. Startup commands (`login`, then first `voir`) are now enqueued and resolved through manager completion, not direct send/wait blocks.
4. Loop mode enqueues periodic commands (`voir`, `inventaire`) into the manager.
5. Incoming server text frames are forwarded to `onServerTextFrame()`.
6. Completed command outcomes are popped and consumed by runner policy code.

This also means low-food behavior is now manager-driven in practice:

- after a successful `inventaire` result is completed,
- the runner parses `nourriture`,
- and if below threshold it enqueues `prend nourriture` through manager flow.

So conceptually, the split is cleaner now:

- `WebsocketClient` handles transport,
- `CommandSender` formats/sends payloads,
- `CommandManager` handles lifecycle (queue, in-flight, retry, completion),
- `ClientRunner` handles high-level policy decisions.

From a milestone perspective, this closes the integration target for the command-management phase foundation, as domain types and specs are in place, manager lifecycle is implemented and tested, and runner orchestration now delegates to manager both at startup and in loop mode. Which leaves, in a pending state for the next round:

- protocol hardening (parser-backed matching, richer failure taxonomy),
- additional integration scenarios (delayed reply, wrong reply type, retry-success),
- AI-facing intent/event API on top of the manager.

<br>
<br>

# 2.4 - Hardening the Protocol

## D1: Reply Matching Hardening - COMPLETE

With the manager framework in place and working, the next immediate priority was to replace the fragile string-substring matching with parser-backed validation. The issue was that the old `matchesInFlightReply()` method used simple substring checks, which meant:

- A malformed JSON frame wouldn't be caught until matching failed
- An unexpected frame (e.g., a voir reply when prend is in-flight) wouldn't be clearly identified
- Protocol errors and structural issues weren't distinguished from command mismatches

The solution was to create a new **`CommandReplyMatcher`** utility class that:

1. **Validates JSON structure** — Ensures frames have basic `{...}` syntax before attempting extraction
2. **Extracts fields carefully** — Parses `"cmd"` and `"arg"` fields with handling for spacing variants (`"cmd":"verb"` vs `"cmd": "verb"`)
3. **Type-specific matching** — Each command type has its own validation logic:
   - `Login`: Accepts any structurally valid frame (no `cmd` field expected)
   - `Voir`: Requires `"cmd": "voir"` field match
   - `Inventaire`: Requires `"cmd": "inventaire"` field match
   - `Prend`: Requires `"cmd": "prend"` and `"arg": "ok"` (rejects `ko` as ServerError)
4. **Clear error classification** — Returns `MatchResult` with:
   - `isMatch`: Whether the frame completes the command
   - `status`: Success, MalformedReply, UnexpectedReply, or ServerError
   - `details`: Diagnostic message (e.g., "Expected 'voir' command, got 'inventaire'")

### New Enum Values

Added to `CommandStatus`:
- `MalformedReply` — Invalid JSON structure or missing required fields
- `UnexpectedReply` — Valid JSON but wrong command type in reply

### Integration into CommandManager

The `onServerTextFrame()` method now:
1. Calls `CommandReplyMatcher::validateReply()` with in-flight command type
2. Handles each status accordingly:
   - `Success` → Completes with success
   - `MalformedReply` → Completes as protocol failure, no retry
   - `UnexpectedReply` → Ignores frame (doesn't consume; next frame may match)
   - `ServerError` → Completes as server failure (error or ko frame)

### Test Coverage

Created `CommandReplyMatcherTest` suite with **32 tests** covering:
- **Valid replies**: Login, Voir, Inventaire, Prend (with spacing variants)
- **Error frames**: Proper ko/error handling
- **Malformed JSON**: Missing fields, incomplete braces, invalid structure
- **Cross-command rejection**: Voir frame doesn't match Prend in-flight
- **Edge cases**: Empty frames, partial JSON, extra fields, field reordering

**Result**: 78/78 tests passing (32 new matcher tests + 46 existing).

This completes Milestone D1. The next step (D2) addresses queue guardrails and additional error taxonomy to prevent protocol-level issues from cascading.

<br>

## D2: Error Taxonomy + Cleanup Safety - COMPLETE

D2 focused on adding safety guardrails and comprehensive logging to prevent resource exhaustion and silent failures:

### Queue Guardrails

Added `MAX_PENDING_COMMANDS = 32` constant to `CommandManager`. The `enqueue()` method now:
- Rejects new commands if queue reaches capacity
- Returns 0 ID on failure (distinguishable from valid command IDs starting at 1)
- Logs a warning when overflow is prevented
- Added public `isFull()` method for callers to check capacity before enqueuing

### Stale In-Flight Protection

Added `STALE_INFLIGHT_MS = 300000` (5 minute) threshold. A new `checkStaleFlight()` method is called each tick and:
- Detects commands that were enqueued more than 5 minutes ago and still in flight
- Force-completes them as Timeout with diagnostic message
- Logs error with time information
- Prevents scenarios where a command hangs forever due to dropped connections or server issues

### Enhanced Error Diagnostics

Completion details now include operational context:
- **Retry context**: "Exhausted retries (X)" shows actual retry limit
- **Dispatch failures**: "Dispatch failed on retry: [specific error]" explains when/why retry failed
- **Unexpected replies**: "Expected 'voir' command, got 'inventaire'" clarifies mismatch
- **Stale timeout**: "Stale command (no response for 300000ms)" indicates protective timeout

### Lifecycle Logging

Every command state transition is now logged with full context:
```
[INFO] CommandManager: Enqueued command 42 (type=5), queue size=8
[INFO] CommandManager: Dispatched command 42
[INFO] CommandManager: Retrying command 42 (attempt 1/2)
[INFO] CommandManager: Command 42 succeeded
[INFO] CommandManager: Command 42 completed with status=0
```

Or on failure:
```
[WARN] CommandManager: Queue overflow prevented. Max pending (32) reached.
[WARN] CommandManager: Command 42 received server error: {"type":"error",...}
[ERROR] CommandManager: Detected stale in-flight command 42 (enqueued 305000ms ago).
```

This provides full observability for debugging command lifecycle issues and validating that no commands are silently dropped.

**Result**: All 78 tests passing (32 new matcher tests + 46 existing tests).


<br>
<br>

# 2.5 - AI-Facing API Bridge

With protocol validation and reliability hardening complete (Milestone D), we now shift focus to the **decision layer abstraction** (Milestone E). The CommandManager provides a robust low-level interface, but higher-level features—like policy-driven decision making and event notification—need a cleaner abstraction. 

## E1: Intent Abstraction Layer

Commands are excellent for transport correctness, but they're low-level primitives. The decision layer needs to think in terms of **intentions** ("Pick up this resource") rather than protocols ("Send PREND command with argument FOO"). We introduce `IntentRequest` as a high-level abstraction:

```cpp
class IntentRequest {
public:
	virtual ~IntentRequest() = default;
	virtual std::string description() const = 0;
};
```

Seven concrete intention types align with observation/action semantics:
- **`RequestVoir`**: Observe visible area and player state
- **`RequestInventaire`**: Query current inventory
- **`RequestTake(ResourceType)`**: Pick up a specific resource
- **`RequestPlace(ResourceType)`**: Drop a resource
- **`RequestMove`**: Advance forward one cell
- **`RequestTurnRight`**: Rotate counterclockwise 90°
- **`RequestTurnLeft`**: Rotate clockwise 90°
- **`RequestBroadcast(message)`**: Send message to all players

Each intent encapsulates arguments (e.g., resource type for Take/Place), enabling the decision layer to work with structured requests while the CommandManager handles protocol translation.

```cpp
struct IntentResult {
	std::uint64_t		id;				// Command ID (for correlation)
	IntentRequest*		intentType;		// Original request type
	bool				succeeded;		// Outcome
	std::string			details;		// Error details if failed
};
```

## E2: Event/Outcome Notification Surface

Commands complete silently into the `_completed` queue, requiring polling. For a responsive decision layer, we introduce **event notifications**—a callback-based observer pattern:

```cpp
struct CommandEvent {
	std::uint64_t	commandId;
	CommandType		commandType;
	CommandStatus	status;
	std::string		details;

	bool isSuccess() const;
	bool isFailure() const;
	std::string statusName() const;
};

using CommandEventHandler = std::function<void(const CommandEvent&)>;
```

`CommandManager` now accepts an optional event handler via `setEventHandler()`, emitting a `CommandEvent` on every command completion:

```cpp
void CommandManager::notifyCompletion(const CommandEvent& event) {
	if (_eventHandler) {
		_eventHandler(event);  // Callback to registered observer(s)
	}
}
```

### Integration Flow

1. **Policy layer** registers event callback: `manager.setEventHandler([this](const CommandEvent& e){ handleOutcome(e); })`
2. **Policy layer** submits intention: `manager.enqueue(CommandType::Voir, nowMs)` (after translating intent)
3. **Manager** queues/dispatches/monitors lifecycle
4. **Server** responds with reply
5. **Manager** validates reply and completes command
6. **Manager** invokes callback: `_eventHandler(commandEvent)`
7. **Policy layer** reacts to outcome via handler callback

This decouples decision logic from command polling, enabling responsive behavior-driven decision making.

### Testing E1/E2

Five integration tests validate the intent-to-event workflow:
- `EventHandlerIsCalledOnCommandCompletion`: Handler receives success events
- `EventHandlerReceivesFailureStatus`: Handler receives failure events on dispatch errors
- `EventHandlerReceivesMultipleEvents`: Sequential command completions trigger sequential callbacks
- `IntentTypesConvertToReadableDescriptions`: Intent polymorphism preserves semantic context
- `CommandEventConvenienceMethodsReturnCorrectStatus`: Event status classification works correctly

**Result**: 83/83 tests passing (5 new intent flow tests + 78 from D-phase).

### Milestone E Architecture

```
┌─────────────────────────────────────────┐
│ Decision Layer (ClientRunner/AI Policy)  │
│  - Uses Intent types, reacts to Events   │
└──────────────────┬──────────────────────┘
                   │
     (enqueue + setEventHandler)
                   │
                   ▼
┌──────────────────────────────────────────┐
│  CommandManager (E2 Event Bridge)         │
│  - Maintains _eventHandler callback      │
│  - Emits CommandEvent on completion      │
└──────────────────┬──────────────────────┘
                   │
        (internal flow: validate/complete)
                   │                 
                   ▼                 ▼
          ┌────────────────┐  ┌─────────────┐
          │ Command Domain │  │ Event Emit  │
          │ (validation)   │  │ (callback)  │
          └────────────────┘  └─────────────┘
```

This completes Milestones E1 and E2: intent abstraction and event notification surfaces are now wired into the CommandManager, ready for AI/policy-driven decision layer integration.

**Test Summary**: 83/83 passing.


This completes Milestone D (reliability hardening). The final step (D3) would add formal observability patterns, but the current logging is sufficient for practical debugging and monitoring.