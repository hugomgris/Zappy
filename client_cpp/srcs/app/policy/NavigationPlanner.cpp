#include "app/policy/NavigationPlanner.hpp"

#include <array>

namespace {
	constexpr std::array<ResourceType, 7> RESOURCE_PRIORITY = {{
		ResourceType::Nourriture,
		ResourceType::Linemate,
		ResourceType::Deraumere,
		ResourceType::Sibur,
		ResourceType::Mendiane,
		ResourceType::Phiras,
		ResourceType::Thystame,
	}};

	std::shared_ptr<IntentRequest> makeIntentForCommand(CommandType type) {
		switch (type) {
			case CommandType::Avance: return std::make_shared<RequestMove>();
			case CommandType::Droite: return std::make_shared<RequestTurnRight>();
			case CommandType::Gauche: return std::make_shared<RequestTurnLeft>();
			default: return nullptr;
		}
	}

	CommandType turnCommandForDirection(int currentOrientation, int desiredOrientation) {
		const int diff = (desiredOrientation - currentOrientation + 4) % 4;
		switch (diff) {
			case 0: return CommandType::Unknown;
			case 1: return CommandType::Droite;
			case 2: return CommandType::Droite;
			case 3: return CommandType::Gauche;
			default: return CommandType::Unknown;
		}
	}

	int applyTurn(int orientation, CommandType commandType) {
		switch (commandType) {
			case CommandType::Droite: return (orientation == 4) ? 1 : (orientation + 1);
			case CommandType::Gauche: return (orientation == 1) ? 4 : (orientation - 1);
			default: return orientation;
		}
	}
}

std::vector<NavigationStep> NavigationPlanner::buildPlan(
	const WorldState& state,
	const std::vector<ResourceType>& resourcePriority
) const {
	const std::optional<std::pair<WorldState::VisionTile, ResourceType>> target = selectTarget(state, resourcePriority);
	if (!target.has_value()) {
		return buildFallbackExplorationPlan();
	}

	const WorldState::VisionTile& tile = target->first;
	const ResourceType resource = target->second;
	if (tile.index == 0) {
		return {{std::make_shared<RequestTake>(resource), CommandType::Prend}};
	}

	const WorldState::Pose pose = state.pose().value_or(WorldState::Pose{});
	const std::pair<int, int> targetDelta = localToWorldDelta(pose, tile.localX, tile.localY);
	return buildTravelPlan(pose, pose.x + targetDelta.first, pose.y + targetDelta.second);
}

std::vector<NavigationStep> NavigationPlanner::buildFallbackExplorationPlan() {
	return {
		{std::make_shared<RequestTurnLeft>(), CommandType::Gauche},
		{std::make_shared<RequestMove>(), CommandType::Avance},
	};
}

std::vector<NavigationStep> NavigationPlanner::buildTravelPlan(
	const WorldState::Pose& pose,
	int targetX,
	int targetY
) {
	std::vector<NavigationStep> plan;
	int currentOrientation = pose.orientation;
	const int currentX = pose.x;
	const int currentY = pose.y;

	const int deltaX = targetX - currentX;
	if (deltaX != 0) {
		const int desiredOrientation = deltaX > 0 ? 2 : 4;
		appendTurnToOrientation(plan, currentOrientation, desiredOrientation);
		currentOrientation = desiredOrientation;
		appendAdvanceSteps(plan, deltaX > 0 ? deltaX : -deltaX);
	}

	const int deltaY = targetY - currentY;
	if (deltaY != 0) {
		const int desiredOrientation = deltaY > 0 ? 1 : 3;
		appendTurnToOrientation(plan, currentOrientation, desiredOrientation);
		appendAdvanceSteps(plan, deltaY > 0 ? deltaY : -deltaY);
	}

	if (plan.empty()) {
		return buildFallbackExplorationPlan();
	}

	return plan;
}

std::pair<int, int> NavigationPlanner::localToWorldDelta(const WorldState::Pose& pose, int localX, int localY) {
	switch (pose.orientation) {
		case 1: return {localX, localY};
		case 2: return {localY, -localX};
		case 3: return {-localX, -localY};
		case 4: return {-localY, localX};
		default: return {localX, localY};
	}
}

void NavigationPlanner::appendTurnToOrientation(
	std::vector<NavigationStep>& plan,
	int currentOrientation,
	int desiredOrientation
) {
	while (currentOrientation != desiredOrientation) {
		const CommandType turnCommand = turnCommandForDirection(currentOrientation, desiredOrientation);
		if (turnCommand == CommandType::Unknown) {
			break;
		}

		plan.push_back({makeIntentForCommand(turnCommand), turnCommand});
		currentOrientation = applyTurn(currentOrientation, turnCommand);
	}
}

void NavigationPlanner::appendAdvanceSteps(std::vector<NavigationStep>& plan, int count) {
	for (int index = 0; index < count; ++index) {
		plan.push_back({std::make_shared<RequestMove>(), CommandType::Avance});
	}
}

std::optional<std::pair<WorldState::VisionTile, ResourceType>> NavigationPlanner::selectTarget(
	const WorldState& state,
	const std::vector<ResourceType>& resourcePriority
) {
	const std::vector<ResourceType> orderedPriority =
		resourcePriority.empty()
			? std::vector<ResourceType>(RESOURCE_PRIORITY.begin(), RESOURCE_PRIORITY.end())
			: resourcePriority;

	for (ResourceType resource : orderedPriority) {
		const std::optional<WorldState::VisionTile> tile = state.nearestVisionTileWith(resource);
		if (tile.has_value()) {
			return std::make_pair(*tile, resource);
		}
	}

	return std::nullopt;
}