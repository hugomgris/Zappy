# Zappy Client - Devlog - 3

## Table of Contents
1. [The Hardest Choices Require The Strongests Wills](#31---the-hardest-choices-require-the-strongests-wills)


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
- For safety, we'll first check if while searching for stones food reservers went below the safe threashold, and if so we'll have the AI transition its state back to `CollectFood`
- Compute missing stones
- If there are no missing stones, state will transition to `Incantating`
- Else we'll have our little dude find the nearest tile with a needed resource, prioritizing distance over anything else (i.e., the check must be done tile-based, not missing-stone-based)
- After finding it, we'll have the little dude plan a stone-targetting navigation

