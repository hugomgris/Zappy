#include "NavigationPlanner.hpp"
#include "helpers/Logger.hpp"

#include <cmath>

namespace zappy {
	NavigationPlanner::NavigationPlanner() {}

	std::string NavigationStep::toString() const {
		switch (action) {
			case NavAction::MoveForward: return "MoveForward";
			case NavAction::TurnLeft: return "TurnLeft";
			case NavAction::TurnRight: return "TurnRight";
			case NavAction::Take: return "Take(" + resource + ")";
			case NavAction::Place: return "Place(" + resource + ")";
			default: return "None";
		}
	}

	std::vector<NavigationStep> NavigationPlanner::planPathToResource(const WorldState& state, const std::string& resource) {
		auto target = state.getNearestItem(resource);
		if (!target.has_value()) {
			return planExploration(state);
		}

		return planPathToTile(state, *target, resource);
	}

	std::vector<NavigationStep> NavigationPlanner::planPathToTile(
			const WorldState& state, const VisionTile& target, const std::string& resource) {
		std::vector<NavigationStep> plan;
		const PlayerState& player = state.getPlayer();

		// if on current tile, just take it!
		if (target.distance == 0) {
			Logger::info("Resource on current tile! Adding take actions");
			for (const auto& item : target.items) {
				// Only take items we actually need
				if (item == resource || item == "nourriture") plan.push_back({NavAction::Take, item});
			}
			return plan;
		}

		// convert local coordinates to world orientation
		auto [worldX, worldY] = localToWorldDelta(target.localX, target.localY, player.orientation);

		Logger::debug("Target at local (" + std::to_string(target.localX) + "," + 
			std::to_string(target.localY) + "), world delta (" + 
			std::to_string(worldX) + "," + std::to_string(worldY) + ")");

		// First move in X direction
		if (worldX != 0) {
			int targetOrientation = (worldX > 0) ? 2 : 4; // either east or west
			auto turns = turnToFace(player.orientation, targetOrientation);
			plan.insert(plan.end(), turns.begin(), turns.end());

			for (int i = 0; i < std::abs(worldX); i++) {
				plan.push_back({NavAction::MoveForward, ""});
			}
		}

		// then move in Y
		if (worldY != 0) {
			int currentOrientation = player.orientation;
			if (!plan.empty()) {
				// this means orientation changed after x mov
				if (worldX > 0) currentOrientation = 2;
				else if (worldX < 0) currentOrientation = 4;
			}

			int targetOrientation = (worldY > 0) ? 3 : 1; // either south or north
			auto turns = turnToFace(currentOrientation, targetOrientation);
			plan.insert(plan.end(), turns.begin(), turns.end());

			for (int i = 0; i < std::abs(worldY); i++) {
				plan.push_back({NavAction::MoveForward, ""});
			}
		}

		// Once we reach the target tile, pick up visible resources there.
		for (const auto& item : target.items) {
			if (item == resource || item == "nourriture") plan.push_back({NavAction::Take, item});
		}
		
		return plan;
	}

	std::vector<NavigationStep> NavigationPlanner::planExploration(const WorldState& state) {
		(void)state;
		std::vector<NavigationStep> plan;

		// Simple random walk pattern
		_explorationStep++;
		
		// Every 3 steps, change direction
		if (_explorationStep % 3 == 0) {
			plan.push_back({NavAction::TurnRight, ""});
		}
		
		// Always move forward
		plan.push_back({NavAction::MoveForward, ""});
		
		// Every 6 steps, also turn left to create a spiral
		if (_explorationStep % 6 == 0) {
			plan.push_back({NavAction::TurnLeft, ""});
		}
		
		return plan;
	}

	std::vector<NavigationStep> NavigationPlanner::planApproachDirection(
			const WorldState& state, int direction) {
		std::vector<NavigationStep> plan;
		const PlayerState& player = state.getPlayer();

		// this is where directions are broadcasted
		// 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW
		int targetOrientation = 1;
		switch(direction) {
			case 1: targetOrientation = 1; break; // NORHT
			case 2: targetOrientation = 1; break; // NE -> go N first
			case 3: targetOrientation = 2; break; // EAST
			case 4: targetOrientation = 2; break; // SE -> go E first;
			case 5: targetOrientation = 3; break;  // South
			case 6: targetOrientation = 3; break;  // SW -> go South first
			case 7: targetOrientation = 4; break;  // West
			case 8: targetOrientation = 4; break;  // NW -> go West first
			default: return plan;
		}

		auto turns = turnToFace(player.orientation, targetOrientation);
		plan.insert(plan.end(), turns.begin(), turns.end());
		plan.push_back({NavAction::MoveForward, ""});

		return plan;
	}

	std::pair<int, int> NavigationPlanner::localToWorldDelta(int localX, int localY, int orientation) const {
		switch (orientation) {
			case 1: return {localX, -localY};      // North
			case 2: return {localY, localX};       // East
			case 3: return {-localX, localY};      // South
			case 4: return {-localY, -localX};     // West
			default: return {localX, localY};
		}
	}

	std::vector<NavigationStep> NavigationPlanner::turnToFace(int currentOrientation, int targetOrientation) const {
		std::vector<NavigationStep> turns;
		
		int diff = (targetOrientation - currentOrientation + 4) % 4;
		
		if (diff == 1) {
			turns.push_back({NavAction::TurnRight, ""});
		} else if (diff == 3) {
			turns.push_back({NavAction::TurnLeft, ""});
		} else if (diff == 2) {
			turns.push_back({NavAction::TurnRight, ""});
			turns.push_back({NavAction::TurnRight, ""});
		}
		
		return turns;
	}
} // namespace zappy