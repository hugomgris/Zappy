#include "app/policy/ResourceStrategy.hpp"

#include <array>

namespace {
	constexpr std::array<ResourceType, 6> STONE_ORDER = {{
		ResourceType::Linemate,
		ResourceType::Deraumere,
		ResourceType::Sibur,
		ResourceType::Mendiane,
		ResourceType::Phiras,
		ResourceType::Thystame,
	}};
}

ResourceStrategy::ResourceStrategy(int foodEmergencyThreshold, int foodComfortTarget)
	: _foodEmergencyThreshold(foodEmergencyThreshold), _foodComfortTarget(foodComfortTarget) {
	_stockTargets[ResourceType::Linemate] = 1;
	_stockTargets[ResourceType::Deraumere] = 1;
	_stockTargets[ResourceType::Sibur] = 1;
	_stockTargets[ResourceType::Mendiane] = 0;
	_stockTargets[ResourceType::Phiras] = 0;
	_stockTargets[ResourceType::Thystame] = 0;
}

std::vector<ResourceType> ResourceStrategy::buildPriority(const WorldState& state) const {
	const int foodCount = currentCount(state, ResourceType::Nourriture);

	if (foodCount < _foodEmergencyThreshold) {
		return {ResourceType::Nourriture};
	}

	std::vector<ResourceType> priority;
	if (foodCount < _foodComfortTarget) {
		priority.push_back(ResourceType::Nourriture);
	}

	const std::vector<ResourceType> deficits = resourceDeficits(state);
	priority.insert(priority.end(), deficits.begin(), deficits.end());

	if (priority.empty()) {
		return {
			ResourceType::Nourriture,
			ResourceType::Linemate,
			ResourceType::Deraumere,
			ResourceType::Sibur,
			ResourceType::Mendiane,
			ResourceType::Phiras,
			ResourceType::Thystame,
		};
	}

	return priority;
}

int ResourceStrategy::currentCount(const WorldState& state, ResourceType resource) const {
	return state.inventoryCount(resource).value_or(0);
}

std::vector<ResourceType> ResourceStrategy::resourceDeficits(const WorldState& state) const {
	std::vector<ResourceType> deficits;
	for (ResourceType resource : STONE_ORDER) {
		const auto targetIt = _stockTargets.find(resource);
		if (targetIt == _stockTargets.end()) {
			continue;
		}

		const int current = currentCount(state, resource);
		if (current < targetIt->second) {
			deficits.push_back(resource);
		}
	}

	return deficits;
}
