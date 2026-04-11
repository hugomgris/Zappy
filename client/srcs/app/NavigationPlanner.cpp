#include "NavigationPlanner.hpp"
#include "../helpers/Logger.hpp"

#include <cmath>

namespace zappy {

NavigationPlanner::NavigationPlanner() {}

std::string NavigationStep::toString() const {
    switch (action) {
        case NavAction::MoveForward: return "MoveForward";
        case NavAction::TurnLeft:    return "TurnLeft";
        case NavAction::TurnRight:   return "TurnRight";
        case NavAction::Take:        return "Take(" + resource + ")";
        case NavAction::Place:       return "Place(" + resource + ")";
        default: return "None";
    }
}

std::vector<NavigationStep> NavigationPlanner::planPathToResource(
        const WorldState& state, const std::string& resource) {
    auto target = state.getNearestItem(resource);
    if (!target.has_value())
        return planExploration(state);
    return planPathToTile(state, *target, resource);
}

std::vector<NavigationStep> NavigationPlanner::planPathToTile(
        const WorldState& state, const VisionTile& target, const std::string& resource) {

    std::vector<NavigationStep> plan;
    const PlayerState& player = state.getPlayer();

    if (target.distance == 0) {
        Logger::info("Resource on current tile! Adding take actions");
        for (const auto& item : target.items) {
            if (item == resource || item == "nourriture")
                plan.push_back({NavAction::Take, item});
        }
        return plan;
    }

    // FIX 6: corrected localToWorldDelta for all four orientations.
    auto [worldX, worldY] = localToWorldDelta(target.localX, target.localY, player.orientation);

    Logger::debug("Target at local (" + std::to_string(target.localX) + "," +
        std::to_string(target.localY) + "), world delta (" +
        std::to_string(worldX) + "," + std::to_string(worldY) + ")");

    // Move in X first
    if (worldX != 0) {
        int targetOrientation = (worldX > 0) ? 2 : 4; // East or West
        auto turns = turnToFace(player.orientation, targetOrientation);
        plan.insert(plan.end(), turns.begin(), turns.end());
        for (int i = 0; i < std::abs(worldX); i++)
            plan.push_back({NavAction::MoveForward, ""});
    }

    // Then Y
    if (worldY != 0) {
        int currentOrientation = player.orientation;
        if (!plan.empty()) {
            if (worldX > 0)      currentOrientation = 2;
            else if (worldX < 0) currentOrientation = 4;
        }
        int targetOrientation = (worldY > 0) ? 3 : 1; // South (+Y) or North (-Y)
        auto turns = turnToFace(currentOrientation, targetOrientation);
        plan.insert(plan.end(), turns.begin(), turns.end());
        for (int i = 0; i < std::abs(worldY); i++)
            plan.push_back({NavAction::MoveForward, ""});
    }

    for (const auto& item : target.items) {
        if (item == resource || item == "nourriture")
            plan.push_back({NavAction::Take, item});
    }

    return plan;
}

std::vector<NavigationStep> NavigationPlanner::planExploration(const WorldState& state) {
    (void)state;
    std::vector<NavigationStep> plan;

    _explorationStep++;

    if (_explorationStep % 3 == 0)
        plan.push_back({NavAction::TurnRight, ""});

    plan.push_back({NavAction::MoveForward, ""});

    if (_explorationStep % 6 == 0)
        plan.push_back({NavAction::TurnLeft, ""});

    return plan;
}

std::vector<NavigationStep> NavigationPlanner::planApproachDirection(
        const WorldState& state, int direction) {

    std::vector<NavigationStep> plan;
    const PlayerState& player = state.getPlayer();

    // Broadcast direction octants: 1=forward, 3=right, 5=behind, 7=left
    // Map to a rotation offset (in 90° steps) from the player's current facing.
    // FIX 6: Use consistent offset-based math instead of hardcoded target orientations.
    int offset = 0; // 0=forward, 1=right, 2=behind, 3=left
    switch (direction) {
        case 1:           offset = 0; break; // straight ahead
        case 2: case 8:   offset = 0; break; // NE/NW → go forward
        case 3: case 4:   offset = 1; break; // E/SE  → go right
        case 5:           offset = 2; break; // S     → go behind
        case 6: case 7:   offset = 3; break; // SW/W  → go left
        default: return plan;
    }

    // Convert offset to absolute orientation (1-indexed: 1=N,2=E,3=S,4=W)
    int targetOrientation = ((player.orientation - 1 + offset) % 4) + 1;

    auto turns = turnToFace(player.orientation, targetOrientation);
    plan.insert(plan.end(), turns.begin(), turns.end());
    plan.push_back({NavAction::MoveForward, ""});

    return plan;
}

/*
 * localToWorldDelta — convert vision-relative (localX, localY) to world-axis delta.
 *
 * Server vision layout (for any facing direction):
 *   localY = 0 is the player's own tile.
 *   localY > 0 = tiles further forward.
 *   localX < 0 = tiles to the left, localX > 0 = tiles to the right.
 *
 * World axes (server): X increases East, Y increases South.
 *
 * FIX 6: The previous EAST case returned {localY, localX} which is correct
 * for the X component but wrong for Y — it should be {localY, localX} only
 * when "right of East" means "south". Let's derive properly:
 *
 *   NORTH (1): forward = −worldY, right = +worldX
 *     worldX =  localX,  worldY = −localY
 *   EAST  (2): forward = +worldX, right = +worldY (south)
 *     worldX =  localY,  worldY =  localX   ← was wrong sign
 *   SOUTH (3): forward = +worldY, right = −worldX
 *     worldX = −localX,  worldY =  localY
 *   WEST  (4): forward = −worldX, right = −worldY (north)
 *     worldX = −localY,  worldY = −localX
 */
std::pair<int, int> NavigationPlanner::localToWorldDelta(
        int localX, int localY, int orientation) const {
    switch (orientation) {
        case 1: return { localX, -localY }; // North
        case 2: return { localY,  localX }; // East  (fixed: worldY = localX not −localX)
        case 3: return {-localX,  localY }; // South
        case 4: return {-localY, -localX }; // West
        default: return { localX,  localY };
    }
}

std::vector<NavigationStep> NavigationPlanner::turnToFace(
        int currentOrientation, int targetOrientation) const {

    std::vector<NavigationStep> turns;
    int diff = (targetOrientation - currentOrientation + 4) % 4;

    if (diff == 1)      turns.push_back({NavAction::TurnRight, ""});
    else if (diff == 3) turns.push_back({NavAction::TurnLeft, ""});
    else if (diff == 2) {
        turns.push_back({NavAction::TurnRight, ""});
        turns.push_back({NavAction::TurnRight, ""});
    }

    return turns;
}

} // namespace zappy
