#pragma once

#include "ProtocolTypes.hpp"
#include "WorldState.hpp"

#include <vector>
#include <string>
#include <optional>

namespace zappy {
	enum class NavAction {
		None,
		MoveForward,
		TurnLeft,
		TurnRight,
		Take,
		Place
	};

	struct NavigationStep {
		NavAction	action;
		std::string	resource;

		std::string toString() const;
	};

	class NavigationPlanner {
		private:
			int	_explorationStep = 0;

			std::pair<int, int> localToWorldDelta(int localX, int localY, int orientation) const;
			std::vector<NavigationStep> turnToFace(int currentOrientation, int targetOrientation) const;

		public:
			NavigationPlanner();
			
			std::vector<NavigationStep> planPathToResource(const WorldState& state, const std::string& resource);
			std::vector<NavigationStep> planPathToTile(const WorldState& state, const VisionTile& target);
			std::vector<NavigationStep> planExploration(const WorldState& state);
			std::vector<NavigationStep> planApproachDirection(const WorldState& state, int direction);
	};
} // namespace zappy