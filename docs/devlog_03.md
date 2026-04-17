# Zappy Client - Devlog - 3

## Table of Contents
1. [The Hardest Choices Require The Strongests Wills](#31---the-hardest-choices-require-the-strongests-wills)
2. [We Don't Walk Alone](#32-we-dont-walk-alone)
3. [Rally-Ho!](#33-rally-ho)


<br>
<br>

# 3.1 - The Hardest Choices Require The Strongests Wills
After reaching or first milestone, we can be sure (or as sure as we can be) about having a working base that 1) acts as a checkpoint in case Charles The Bot The Third kicks the bucket 2) makes the road ahead a pure matter of extending. This means that our next milestone, which is to have a single little dude reach max level by itself in easy mode, as well as anything that comes later, is just going to ask us to work on adding layers of possibility and complexity to `Behavior` and `Navigator`. Tools and pipelines for stone gathering, rallying and forking, those things that are still pending between us and a full, non-easy winning game bot. I trust in Charles III, but we'll have to see if it believes in itself.

So, as I said, the first goal going forward is to make our little dude gather stones, Thanos-style, and our first small step should be to make our probed single little dude to reach level 2, as the 1→2 transition is the only one that doesn't require aditional participants beyond the S E L F. Doing so is going to require:
- New `CollectStones` and `Incantation` states added to the enum of the machine
- A table of stone and player requirements for each level transition (we'll only use the first one for now, but we'll need it for the next step so let's get it out of our way)
- Create transition patters between them
- Maintain a safe lock for emergency food levels (i.e., triangulate these two new states with the already existing `CollectFood`)
- Add navigation path searching targetting stones
- Add incantation triggers and managers

That sould do it. And with or new list in our hands, let's get into implementations. First, and after extending the `AIState` collection, we'll expand `Behavior`'s attributes to track its `AIState`, and three level-up related additions (`_stonesNeeded`, `_incantationSet`, `_stonesPlaced`) to track the progress of the ascension ritual. After this, our next important change is related to how `tick()` works in `Behavior`. Up until this point, our little dude has just been following a fixed pattern without regards to its state, but we have to transition into a proper state-machine like state-depending ticking. To do so, we'll refactor `tick()` to be an entry point to the ticking process, containing just a switch case on the current `AIState` in order to derive execution to specific ticking sub-functions, all while keeping the existing top-checks. Something like this:
```cpp
void Behavior::tick(int64_t nowMs) {
    if (hasCommandInFlight()) return;
    if (isVisionStale())      { refreshVision(); return; }
    if (isInventoryStale())   { refreshInventory(); return; }

    switch (_aiState) {
        case AIState::CollectFood:      tickCollectFood(); break;
        case AIState::CollectStones:    tickCollectStones(); break;
        case AIState::Incantating:      tickIncantating(); break;
        case AIState::Idle:             break;
    }
}
```

For clarity sakes, we'll get the refreshing stuff to specific functions. Then, everything else that was inside `tick()`, which is basically the food-gathering behavior, will be taken to `tickCollectFood()`, as-is, just with the state transition check and execution added to the top of the function. A very simple one: **if the amount of food in inventory is higher than the safe threshold set up as `FOOD_SAFE`, `_aiState` will go from `CollectFood` to `CollectStones`. Easy stuff.

Now, for the stone collection behavior, housed in `tickCollectStones()`, we'll need to do the following:
- For safety, we'll first check if while searching for stones food reservers went below the critical threashold, and if so we'll have the AI transition its state back to `CollectFood`
	- We'll make this check against `FOOD_CRITICAL` and not `FOOD_SAFE` because if did so, we'll have a little dude ping-pong-ing between states, looping endlessly, lost forever.
- Compute missing stones
- If there are no missing stones, state will transition to `Incantating`
- Else we'll have our little dude find the nearest tile with a needed resource, prioritizing distance over anything else (i.e., the check must be done tile-based, not missing-stone-based)
- After finding it, we'll have the little dude plan a stone-targetting navigation

Writing this logic is not a big deal. At the end of the day, `tickCollectStones()` is one of those flow functions in which what's really important is the order of checks and operations. The AI needs to know what stones it needs to search for, and once it knows what it is looking for, the logic is practically the same as looking for `nourriture` items. So, really, the important stuff here is the implementation of a couple of helpers, `computeMissingStones()` to acquire stone targets, and `getNearestTileWithNeededResourcer()` to locate them.

`ComputeMissingStones()` is just a checkup against the pre-made table of requirements per level transition. A `LevelReq` struct placed in `Behaviour.hpp`, tracking the amount of players for an incantation ritual, and the specific amount of stones (as an `Inventory` object) suffices to set up a static function that creates the mentioned table. Then it's just a question of pure comparison and detection:
```cpp
static const LevelReq& levelReq(int level) {
	// recipe: index 0 = level 1->2, index 6 = level 7->8
	// order: players -> nourriture, linemate, deraumere, sibur, mendiane, phiras, thystame
	static const LevelReq table[7] = {
		{ 1, { 0, 1, 0, 0, 0, 0, 0 } }, // 1→2
        { 2, { 0, 1, 1, 1, 0, 0, 0 } }, // 2→3
        { 2, { 0, 2, 0, 1, 0, 2, 0 } }, // 3→4
        { 4, { 0, 1, 1, 2, 0, 1, 0 } }, // 4→5
        { 4, { 0, 1, 2, 1, 3, 0, 0 } }, // 5→6
        { 6, { 0, 1, 2, 3, 0, 1, 0 } }, // 6→7
        { 6, { 0, 2, 2, 2, 2, 2, 1 } }, // 7→8
	};

	if (level < 1 || level > 7)
		return table[0]; // never going to happen, but its a safe fallback
	return table[level - 1];
}
```
```cpp
void Behavior::computeMissingStones() {
	if (_state.vision.empty()) return;
	
	int currentLevel = _state.player.level;
	Inventory& inv = _state.player.inventory;
	
	_stonesNeeded.clear();
	
	// easy mode -> 1 lineamte is enough
	if (_easyMode) {
		if (inv.linemate < 1) {
			_stonesNeeded.push_back("linemate");
		}
		return;
	}
	
	// normal mode
	const LevelReq& requirements = levelReq(currentLevel);
	if (requirements.stones.linemate  > inv.linemate)  _stonesNeeded.push_back("linemate");
	if (requirements.stones.deraumere > inv.deraumere) _stonesNeeded.push_back("deraumere");
	if (requirements.stones.sibur     > inv.sibur)     _stonesNeeded.push_back("sibur");
	if (requirements.stones.mendiane  > inv.mendiane)  _stonesNeeded.push_back("mendiane");
	if (requirements.stones.phiras    > inv.phiras)    _stonesNeeded.push_back("phiras");
	if (requirements.stones.thystame  > inv.thystame)  _stonesNeeded.push_back("thystame");
}
```

This all that our little guys need to roam around and gather stones, wrapped around some state transitions that take them back to food gathering if they go below the emergency threshold, and go forward into `Incantating` state if the set of required stones are in their inventory. A couple of notes here, though:
- When I first wrote `tickCollectStones()`, I considered the stones in the current tile of the little guy as part of a total sum for the requirement comparison. This didn't work properly. There's risks to doing so because the client might pass, say, a linemate check because there's linemate in its current position, but after detecting that it needs to move to get some deraumere that check would fail, and maybe this happens after confirming that deraumere is in place, but not because it was picked but because the target, deraumere-containing tile was reached... And so on and so forth. I guess that there must be a way to consider the stones already existing in tiles to make the whole game-playing process more optimal, but it feels like it would be difficult to pinpoint it and not worth it. So, for now and most likely forever, the requirements for a level up transition will only be checked against the client's inventory.
- At this stage/milestone, we're just working with a single little guy, which can reach max level and win the game if the session is set up in easy mode (via the env variable `ZAPPY_EASY_ASCENSION=1`). Until we transition to the next milestone, concerning multi-client cooperation, special considerations regarding the "role" a client assumes when going into `Incantating` state are irrelevant, i.e. not yet implemented. Maybe I'll have to set up class attributes to track the existance and identity of a "leader", I don't know. We'll see

What's important is that with all of the above in place, `tickCollectStones()` can be written:
```cpp
void Behavior::tickCollectStones() {
    if (_state.player.food() < FOOD_CRITICAL) {
        _aiState = AIState::CollectFood;
        clearNavPlan();
        return;
    }

    computeMissingStones();

    if (_stonesNeeded.empty()) {
        _aiState = AIState::Incantating;
		_incantationReady = false;
        clearNavPlan();
        return;
    }

    // pick up needed stone if already standing on one
    for (const auto& stone : _stonesNeeded) {
        if (_state.countItemOnCurrentTile(stone)) {
            clearNavPlan();
            _commandInFlight = true;
            _sender.sendPrend(stone);
            _sender.expect("prend " + stone, [this](const ServerMessage& msg) {
                _commandInFlight = false;
                if (msg.isOk())
                    setInventoryStale();
                setVisionStale();
            });
            return;
        }
    }

    // navigate towards nearest needed resource
    clearNavPlan();
    auto tile = getNearestTileWithNeededResource();

    if (tile.localX == std::numeric_limits<int>::max()) {
        // nothing visible yet->explore
        std::vector<NavCmd> plan = Navigator::explorationStep(_explorationStep);
        _navPlan.assign(plan.begin(), plan.end());
        _navTarget.clear();
    } else {
        std::vector<NavCmd> plan = Navigator::planPath(
            _state.player.orientation, tile.localX, tile.localY);
        Logger::debug("Behavior: CollectStones: planned " + std::to_string(plan.size()) +
            " steps to " + _navTarget + " at (" +
            std::to_string(tile.localX) + "," + std::to_string(tile.localY) + ")");
        _navPlan.assign(plan.begin(), plan.end());
    }

    if (!_navPlan.empty()) {
        NavCmd next = _navPlan.front();
        _navPlan.pop_front();
        executeNavCmd(next);
    }
}
```

`tickIncantating()` is going to be a little bit trickier, though. But we can do it, remember, we have a system for tackling things: let's think about what this needs to do. Everything we need is in of these places: past logs, our head, our heart, God. So we just have to connect with the correct source of knowledge to build know that the function is going to need the following steps:
1. Be sure that it's working in a non-stale vision state, triggering a `refreshVision()` call before moving on
2. Place the required stones for the level transition in the floor (as stated by the rules of the game).
	- This is the most tricky part, really, but deep down is just a thorough cross-check between the transition requirements, the tile contents and the little dude's inventory, which has to halt incantation ticking progress until all stones are in place.
3. The third step is a safety check wrapping the previous step, a way of being 100% sure that the tile contains the necessar stones
4. Once the previous check passes, `_sender.sendIncantation()` can be fired, with some detailed handling of the callback sent to the `expect()` function, as incantation is a two step process: it get's requested and aknowledge first, it gets resolved second.
5. Whatever the result is once an incantation is fired, the exit transition from `Incantating` is set up towards `CollectStones`. Back there, food related concernes will be managed, and the client will keep on playing the game normally.

In code, all of this looks like the following, super cute function:
```cpp
void Behavior::tickIncantating() {
	// Step 1: Get a fresh vision first
	if (!_incantationReady) {
		setVisionStale();
		_incantationReady = true;
		return;
	}

	// Step 2: Place required stones (one per tick)
	if (!_stonesPlaced) {
		if (_easyMode) {
			// Easy mode: only need to place 1 linemate
			auto& tile = _state.vision[0];
			if (tile.countItem("linemate") < 1) {
				_commandInFlight = true;
				_sender.sendPose("linemate");
				_sender.expect("pose linemate", [this](const ServerMessage& msg) {
					_commandInFlight = false;
					if (msg.isOk()) {
						_state.player.inventory.linemate--;
						setInventoryStale();
					}
					setVisionStale();
				});
				return;
			}
		} else {
			// Normal mode: place all required stones
			auto requirements = levelReq(_state.player.level);
			auto& tile = _state.vision[0];

			#define TRY_POSE(stone_name) \
				if (requirements.stones.stone_name > 0 && \
					tile.countItem(#stone_name) < requirements.stones.stone_name) { \
					_commandInFlight = true; \
					_sender.sendPose(#stone_name); \
					_sender.expect("pose " #stone_name, [this](const ServerMessage& msg) { \
						_commandInFlight = false; \
						if (msg.isOk()) { \
							_state.player.inventory.stone_name--; \
							setInventoryStale(); \
						} \
						setVisionStale(); \
					}); \
					return; \
				}

			TRY_POSE(linemate)
			TRY_POSE(deraumere)
			TRY_POSE(sibur)
			TRY_POSE(mendiane)
			TRY_POSE(phiras)
			TRY_POSE(thystame)
			#undef TRY_POSE
		}

		_stonesPlaced = true;
		setVisionStale();
		return;
	}

	// Step 3: Verify stones - simplified for easy mode
	if (_staleVision) return;
	
	auto& tile = _state.vision[0];
	
	bool stonesOk;
	if (_easyMode) {
		stonesOk = tile.countItem("linemate") >= 1;
	} else {
		auto requirements = levelReq(_state.player.level);
		stonesOk = tile.countItem("linemate")  >= requirements.stones.linemate  &&
				tile.countItem("deraumere") >= requirements.stones.deraumere &&
				tile.countItem("sibur")     >= requirements.stones.sibur     &&
				tile.countItem("mendiane")  >= requirements.stones.mendiane  &&
				tile.countItem("phiras")    >= requirements.stones.phiras    &&
				tile.countItem("thystame")  >= requirements.stones.thystame;
	}

	if (!stonesOk) {
		Logger::warn("Behavior: stones missing on tile after placement, back to CollectStones");
		_stonesPlaced = false;
		_incantationReady = false;
		_aiState = AIState::CollectStones;
		return;
	}

	// Step 4: Send incantation
	_commandInFlight = true;
	_sender.sendIncantation();
	_sender.expect("incantation", [this](const ServerMessage& msg) {
		if (msg.isInProgress()) {
			return;
		}

		_commandInFlight = false;
		_stonesPlaced = false;
		_incantationReady = false;

		if (_pendingLevelUp) {
			_state.player.level++;
			Logger::info("Level up! Now level " + std::to_string(_state.player.level));
			_pendingLevelUp = false;
			_aiState = (_state.player.level >= 8) ? AIState::Idle : AIState::CollectStones;
		} else {
			Logger::warn("Incantation failed (ko or timeout), restarting stone collection");
			_aiState = AIState::CollectStones;
		}
	});
}
```
And now, assuming that everything is correctly coded, what we should expect when running a single client probe test with the running server in easy mode is a little dude thriving, surviving, ascending and winning. There are some edits that need to be done here and there, both in client and server, to correctly handle the mentioned safe mode, detect winning conditions and trigger the endgame, but those are just uninteresting work. Which I've allready done...

... And my little dude is working correctly!!! And I'm happy!!! And the current milestone is done!! And we're almost there!! Not really because what's left is kind of the hardes part but hey we're fine!!! We're alive!!!

<br>
<br>

# 3.2 We Don't Walk Alone
Our next milestone is to extend the stone gathering behavior so that **every level requirements are correctly gathered** in a non-easy context. We'll have to be careful to not break the 1→2 trasition (which little dudes can do by themselves), but we'll have to test level after level if the clients target the rocks the are supposed to. We'll do so while also injecting some resource type priorities (rarest > most common) and some opportunistic sub-behavior, like picking up food while moving if it exist in the passing tiles. We'll also add the first `fork()` related logic along our way, tied to `Idle` state. A bunch of things, but all bounded to `Behavior`, so we'll be fine. I'm sure.

Inserting priority and opportunity criteria in our Behavior is a matter of fine tunning stuff, which is to say know what to change with extreme specificity and try to do so without ending up with a broken state of things. Which is what happened to me a couple of times after editing the code. 

You see, encoding the priority order is not that complicated. We just need a `STONE_PRIORITY` constant to chec against and change how `computeMissingStones()` and `getNearestTileWithNeededResources()` work with the new priority in mind. Transitioning a priority-ordered loop in the first one makes it so the order in the class vector `_stonesNeeded` follows the fixed importance order. Then, instead of going through all the list and commit the target tile to the pure nearest, we switch to the **nearest with the resource with most priority**. This leaves us in a very specic logic state:
- There's room to add more complexity to this decision routine so that it is *wieghted*, wich means that it based on a ponderation of priority and closeness, so that the client doesn't target whatever it saw with a high priority when it is too far away in relation to some other thing that's considerable close but has a lowest priority. There's really no need for this, but it would be cool to do, so we'll toss it in the *maybe* box of our to-do list.
- The new priority focus navigation planification gives room for the opportunity layer, as now the little dudes might walk further, but we can take advantage of whatever resource is present in the passing tiles. That is: if there's food, just pick it, but also *if theres some non-targetted stone that turns out to be needed*, well, why not pick it up?

All of this has extended the `tickCollectStones()` function, which now looks like this:
```cpp
void Behavior::tickCollectStones() {
	if (_state.player.food() < FOOD_CRITICAL) {
		_aiState = AIState::CollectFood;
		clearNavPlan();
		return;
	}

	computeMissingStones();

	if (_stonesNeeded.empty()) {
		_aiState = AIState::Incantating;
		_incantationReady = false;
		clearNavPlan();
		return;
	}

	// pick up needed stone if already standing on one
	for (const auto& stone : _stonesNeeded) {
		if (_state.countItemOnCurrentTile(stone)) {
			clearNavPlan();
			_commandInFlight = true;
			_sender.sendPrend(stone);
			_sender.expect("prend " + stone, [this](const ServerMessage& msg) {
				_commandInFlight = false;
				if (msg.isOk())
					setInventoryStale();
				setVisionStale();
			});
			return;
		}
	}

	// Opportunistic food grab: don't interrupt a collection run, but take free food if it's right here
	if (_state.player.food() < FOOD_SAFE &&
		_state.countItemOnCurrentTile("nourriture")) {
		_commandInFlight = true;
		_sender.sendPrend("nourriture");
		_sender.expect("prend nourriture", [this](const ServerMessage& msg) {
			_commandInFlight = false;
			if (msg.isOk()) {
				_state.player.inventory.nourriture++;
				setInventoryStale();
			}
			setVisionStale();
		});
		return;
	}

	std::string previousTarget = _navTarget;
	auto tile = getNearestTileWithNeededResource();

	if (tile.localX == std::numeric_limits<int>::max()) {
		if (_navPlan.empty()) {
			std::vector<NavCmd> plan = Navigator::explorationStep(_explorationStep);
			_navPlan.assign(plan.begin(), plan.end());
			_navTarget.clear();
		}
	} else if (_navPlan.empty() || _navTarget != previousTarget) {
		std::vector<NavCmd> plan = Navigator::planPath(
			_state.player.orientation, tile.localX, tile.localY);
		Logger::debug("Behavior: CollectStones: planned " + std::to_string(plan.size()) +
			" steps to " + _navTarget + " at (" +
			std::to_string(tile.localX) + "," + std::to_string(tile.localY) + ")");
		_navPlan.assign(plan.begin(), plan.end());
	}

	if (!_navPlan.empty()) {
		NavCmd next = _navPlan.front();
		_navPlan.pop_front();
		executeNavCmd(next);
	}
}
```

One immediate consequence of this extension is that the single little dude probe is now winning the game considerably faster. From a general perspective, this is happenning because the current logic, after the above logged edits, is less "chopped", less compartimentalized around food and stones, flowing better in its navigation and picking stuff up along the carried out paths. A good sign, if you ask me.

Let's now focus on the initial fork logic injection. Nothing fancy, the only thing that needs some thought is *where* should fork be handled. Some design constraints around this (besides the decision around clients being able to fork multiple times or just one, irrelevant atm) are:
- Given that clients need to fork when in conditions are safe enough, how would we mark those? Easy: set up a `FOOD_FORK` thredshold, higher than `FOOD_SAFE` and, of course, quite higher than `FOOD_CRITICAL`. I'll set it to `20`.
- Given that the set value is higher than the safe condition, the only place in the current behavior structure were a fork could actually happen is in `CollectStones` state. And because `tickCollectStones()` makes a food safe check at the top, then goes into proper stone search, a good place to inject this forking bussiness is right in the middle of those. This will give us the following flow when in `CollectStones` state:
	- Check if food levels are safe enough to continue in this state
	- Check if food levels are high enough to attempt a fork, and if so do so
	- Continue with the stone gathering logic towards achieving incantatation requirements
- Given that it doesn't really make that much sense to have level-1 clients forking themselves, will set the bar to at least them being level 2

Besides this, the fork call itself is nothing we haven't done quite a lot of times at this stage. We'll soon have to tweak some stuff regarding multi-client coordination, but for now this is more than enough:
```cpp
// TODO: decide if clients should have limited fork capabilities/shots or just fork whenever food is high enough
	if (_state.player.food() > FOOD_FORK && _state.player.level >=2 && _state.forkEnabled) {
		Logger::info("Fork call triggered");
		_aiState = AIState::Idle;
		clearNavPlan();
		_commandInFlight = true;
		_sender.sendFork();
		_sender.expect("fork", [this](const ServerMessage& msg) {
			(void)msg;
			_aiState = AIState::CollectStones;
			setVisionStale();
			setInventoryStale();
			_forkInProgress = false;
			_commandInFlight = false;
		});
		_forkInProgress = true;
		return;
	}
```
> *At this moment, I'm using `Idle` state as a pretty much useless track state for forking status. I'll get rid of this in the next steps, repurposing `Idle` to work towards multi-client leveling up and rallying.*

<br>
<br>

# 3.3 Rally-Ho!