#pragma once

#include "app/command/CommandType.hpp"
#include "app/intent/Intent.hpp"
#include "app/state/WorldState.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

struct NavigationStep {
	std::shared_ptr<IntentRequest> intent;
	CommandType commandType = CommandType::Unknown;
};

class NavigationPlanner {
	public:
		std::vector<NavigationStep> buildPlan(
			const WorldState& state,
			const std::vector<ResourceType>& resourcePriority = {}
		) const;

	private:
		static std::vector<NavigationStep> buildFallbackExplorationPlan();
		static std::vector<NavigationStep> buildTravelPlan(
			const WorldState::Pose& pose,
			int targetX,
			int targetY
		);
		static std::pair<int, int> localToWorldDelta(const WorldState::Pose& pose, int localX, int localY);
		static void appendTurnToOrientation(
			std::vector<NavigationStep>& plan,
			int currentOrientation,
			int desiredOrientation
		);
		static void appendAdvanceSteps(std::vector<NavigationStep>& plan, int count);
		static std::optional<std::pair<WorldState::VisionTile, ResourceType>> selectTarget(
			const WorldState& state,
			const std::vector<ResourceType>& resourcePriority
		);
};