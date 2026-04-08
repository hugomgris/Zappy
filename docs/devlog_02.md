# Zappy Client - Devlog - 2

## Table of Contents
1. [Command Center](#21---command-center)
2. [I Want to Talk to the Commander!](#22-i-want-to-talk-to-the-commander)
3. [Commander Takes the Wheel](#23---commander-takes-the-wheel)
4. [Hardening the Protocol](#24---hardening-the-protocol)
5. [AI-Facing API Bridge](#25---ai-facing-api-bridge)
6. [Games are Political (or Policy Layer Integration)](#26---games-are-political-or-policy-layer-integration)
7. [Correlation and Validation Upgrade](#27---correlation-and-validation-upgrade)
8. [World-State Foundation](#28---world-state-foundation)
9. [Navigation and Local Planning](#29---navigation-and-local-planning)
10. [Incantation Readiness and Timing](#210---incantation-readiness-and-timing)
11. [Team Coordination via Broadcast](#211---team-coordination-via-broadcast)
12. [Gameplay Validation Harness](#212---gameplay-validation-harness)


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

With the manager framework in place and working, the next immediate priority was to replace the fragile string-substring matching with parser-backed validation. The issue was that the old `matchesInFlightReply()` method used simple substring checks, which meant:

- A malformed JSON frame wouldn't be caught until matching failed
- An unexpected frame (a voir reply when prend is in-flight, for example) wouldn't be clearly identified
- Protocol errors and structural issues weren't distinguished from command mismatches

The solution was to create a new **`CommandReplyMatcher`** utility class that:

1. **Validates JSON structure**, ensuring frames have basic `{...}` syntax before attempting extraction
2. **Extracts fields carefully**, parsing `"cmd"` and `"arg"` fields with handling for spacing variants (`"cmd":"verb"` vs `"cmd": "verb"`; needed because of how the server formats is JSON messages)
3. **Type-specific matching**, with each command type having its own validation logic:
   - `Login`: Accepts any structurally valid frame (no `cmd` field expected)
   - `Voir`: Requires `"cmd": "voir"` field match
   - `Inventaire`: Requires `"cmd": "inventaire"` field match
   - `Prend`: Requires `"cmd": "prend"` and `"arg": "ok"` (rejects `ko` as ServerError)
4. **Clear error classification** — Returns `MatchResult` with:
   - `isMatch`: Whether the frame completes the command
   - `status`: Success, MalformedReply, UnexpectedReply, or ServerError
   - `details`: Diagnostic message (e.g., "Expected 'voir' command, got 'inventaire'")

To achieve these, new enums needed to be added to `CommandStatus`:
- `MalformedReply` — Invalid JSON structure or missing required fields
- `UnexpectedReply` — Valid JSON but wrong command type in reply

And the `onServerTextFrame()` method now:
1. Calls `CommandReplyMatcher::validateReply()` with in-flight command type
2. Handles each status accordingly:
   - `Success` → Completes with success
   - `MalformedReply` → Completes as protocol failure, no retry
   - `UnexpectedReply` → Ignores frame (doesn't consume; next frame may match)
   - `ServerError` → Completes as server failure (error or ko frame)

<br>
<br>

# 2.5 - AI vs API Bridge

With protocol validation and reliability hardening complete, focus needs to be switched now to the **decision layer abstraction**. The CommandManager provides a robust low-level interface, but higher-level features—like policy-driven decision making and event notification—need a cleaner abstraction. This is because commands are excellent for transport correctness, but they're low-level primitives. The decision layer needs to think in terms of **intentions** ("Pick up this resource") rather than protocols ("Send PREND command with argument FOO"). To this aim, we introduce `IntentRequest` as a high-level abstraction, around which the specific command-related implementations are built:

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

From here, due to commands completing silently into the `_completed` queue, and requiring polling,in order to achieve a responsive decision layer, we introduce **event notifications**, a callback-based observer pattern:

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

### Current Integration Flow

Just to not get lost in our own sauce, I think its a good idea if we stop, make an ordered list of the current flow steps of the client and resituate ourselves in regards to what should be done next.

1. **Policy layer** registers event callback: `manager.setEventHandler([this](const CommandEvent& e){ handleOutcome(e); })`
2. **Policy layer** submits intention: `manager.enqueue(CommandType::Voir, nowMs)` (after translating intent)
3. **Manager** queues/dispatches/monitors lifecycle
4. **Server** responds with reply
5. **Manager** validates reply and completes command
6. **Manager** invokes callback: `_eventHandler(commandEvent)`
7. **Policy layer** reacts to outcome via handler callback


```
┌─────────────────────────────────────────┐
│ Decision Layer (ClientRunner/AI Policy) │
│  - Uses Intent types, reacts to Events  │
└──────────────────┬──────────────────────┘
                   │
     (enqueue + setEventHandler)
                   │
                   ▼
┌──────────────────────────────────────────┐
│  CommandManager (E2 Event Bridge)        │
│  - Maintains _eventHandler callback      │
│  - Emits CommandEvent on completion      │
└──────────────────┬───────────────────────┘
                   │
        (internal flow: validate/complete)
                   │                 
                   ▼                 ▼
          ┌────────────────┐  ┌─────────────┐
          │ Command Domain │  │ Event Emit  │
          │ (validation)   │  │ (callback)  │
          └────────────────┘  └─────────────┘
```

<br>
<br>

# 2.6 - Games are Political (or Policy Layer Integration)

Before moving into full gameplay behavior, the client architecture needs to be pushed one step further so that runtime orchestration can be policy-driven instead of hardcoded in the runner loop.

The key addition in this very moment is a new `DecisionPolicy` abstraction with two hooks:

- `onTick(nowMs)`: emits intents on periodic cadence
- `onCommandEvent(nowMs, event, intentResult)`: reacts to outcomes and can chain follow-up intents

This gives us a clean contract between command infrastructure and future AI behavior. The runner no longer needs to embed concrete decision rules to keep the loop alive. To keep current behavior, we also added a default `PeriodicScanPolicy` used by loop mode when no explicit policy is injected. It reproduces the old periodic scan behavior (`voir` and `inventaire`) while keeping that behavior outside transport orchestration. Additionally, `ClientRunner` gained command-layer APIs so integration tests can run deterministic command cycles without requiring a real websocket session:

- `tickCommandLayerForTesting(...)`
- `processManagedTextFrameForTesting(...)`

That is an important enabler for faster iteration on the decision layer.

<br>
<br>

# 2.7 - Correlation and Validation Upgrade

Another important improvement that's needed before going into full gameplay implementations mode is making intent/command correlation explicit in `ClientRunner`. When intents are submitted, the runner now stores command id to intent description mappings. When a command completes, it emits/records an `IntentResult` tied to the originating intent semantics (instead of only low-level command status). This means we can now:

- correlate policy decisions to concrete command completions,
- consume completion records through a dedicated queue (`popCompletedIntent`),
- and subscribe to completion callbacks via `setIntentCompletionHandler(...)`.

In practical terms, this closes a key observability gap: policy code can reason about outcomes in terms of intent language, not just protocol language. Which also makes it *easier*, or at least *clearer* for when the gameplay programming moment arrives.

<br>
<br>

# 2.8 - World-State Foundation

With the policy seam stable, the next step was to stop treating command outcomes as isolated events and start keeping a reusable memory of the world. That means the client now has a dedicated `WorldState` model that stores:

- the last successful `voir` payload,
- the last successful `inventaire` payload,
- parsed inventory counts by resource type,
- and the last broadcast-related semantic payload.

To make that state useful, `WorldModelPolicy` was introduced as the first stateful policy implementation. It consumes command completion events, updates the world model, and asks for fresh `voir` / `inventaire` data when observations are missing or stale.

The important distinction here is that we are no longer just moving commands around correctly. We now have a real memory layer that future planning logic can consult.

<br>
<br>

# 2.9 - Navigation and Local Planning

The first gameplay-oriented layer on top of the world model is now in place. Rather than blindly alternating between refresh commands, the client can now inspect the latest visible resources, pick a target, and translate that target into a small movement plan.

The new `NavigationPlanner` is intentionally simple:

- it prefers visible resources in a fixed priority order,
- it converts a target tile into `turn` and `avance` steps,
- it treats the current tile as a `take` opportunity,
- and it falls back to a basic exploration step when nothing worth pursuing is visible.

`WorldModelPolicy` now keeps that planner in the loop, clears the plan when fresh vision arrives, and updates pose memory when movement and turn commands succeed. That gives us the first closed loop where observations drive a plan, and plan completion updates the model for the next decision.

This is still a deliberately conservative planner, but it is the first point where the client starts acting like a bot instead of a command scheduler.

<br>
<br>

# 2.10 - Incantation Readiness and Timing

With local planning and resource strategy in place, the next step was to make incantation a readiness-driven decision instead of a blind command. The policy layer now evaluates whether ascension should be attempted immediately, deferred, or converted into a summon request.

This update landed in three parts:

- Added a dedicated `RequestIncantation` intent so ascension attempts stay in intent language through policy and runner flows.
- Added `IncantationStrategy` with explicit readiness rules based on:
	- minimum safe food,
	- required inventory set,
	- minimum players on the current tile.
- Extended `WorldState` vision parsing to count `player` tokens per tile, exposing `currentTilePlayerCount()` to the policy layer.

`WorldModelPolicy` now evaluates incantation readiness before normal navigation planning:

- when preconditions are met, it emits `RequestIncantation`;
- when resources are ready but player count is insufficient, it emits a summon broadcast;
- when preconditions are not met, it falls back to gather/navigation behavior.

Timing guardrails were added as well:

- incantation retry delay,
- summon broadcast cooldown.

Those delays prevent spam loops and create a safer transition into team-coordination behavior for the next milestone.

<br>
<br>

# 2.11 - Team Coordination via Broadcast

The next milestone after readiness timing was to turn broadcasts into an actual coordination channel, not only a summon fallback. The client now understands a lightweight team protocol and can react to teammate messages in policy space.

Three concrete pieces landed:

- Added `TeamBroadcastProtocol` as a shared parser for team messages. It supports both new structured messages (`team:need:players`, `team:need:food`, role announcements) and legacy summon phrasing (`need_players_for_incantation`).
- Extended `WorldModelPolicy` with team-coordination rules:
	- parse incoming team broadcasts,
	- apply food-safety-first priority rules,
	- emit cooperative responses (`team:on_my_way`, `team:offer:food`) under cooldown,
	- periodically publish local role intent (`team:role:gatherer`).
- Updated `ClientRunner` frame handling so unsolicited server frames with `"type":"message"` are routed into policy events (`CommandType::Broadcast`) even when there is no in-flight command waiting for completion.

This keeps command correlation intact while letting the policy react to teammate communication as first-class input.

Coverage for this milestone includes:

- `TeamBroadcastProtocolTest` for protocol parsing and compatibility.
- `WorldModelPolicyTest` cases for incoming team requests, food-priority conflict resolution, and role-cadence broadcasts.
- `ClientRunnerTeamMessageIntegrationTest` for end-to-end unsolicited-message routing from frame to follow-up intent dispatch.

With this in place, coordination decisions are now visible in logs and deterministic tests, and the policy can participate in at least one cooperative scenario without introducing transport-level coupling.

<br>
<br>

# 2.12 - Gameplay Validation Harness

With I5 in place, the next step was to validate behavior over realistic sequences rather than only isolated unit checks. This milestone focused on scenario-driven integration tests and a long-run invariant loop.

The new `WorldModelPolicyScenarioIntegrationTest` suite adds four high-value cases:

- **Starvation prevention under sparse food**:
	- when food is low and no food is visible, policy explores;
	- when food appears on current tile, policy immediately prioritizes `RequestTake(nourriture)`.
- **Gather and prepare for incantation**:
	- policy gathers missing incantation resources from vision,
	- then triggers `RequestIncantation` once inventory and player preconditions are satisfied.
- **Reroute after stale/incorrect assumptions**:
	- policy starts from an initial travel plan,
	- then clears/replans immediately when fresh `voir` contradicts old assumptions.
- **Long-run loop validation (1200 ticks)**:
	- simulated command/event feedback over extended runtime,
	- verifies no deadloop behavior,
	- enforces food-priority invariant when emergency conditions and visible food coincide.

This closes I6 acceptance with deterministic coverage and keeps regression confidence high for the next polish milestones.
