#pragma once

#include "app/command/ResourceType.hpp"
#include "app/state/WorldState.hpp"

#include <map>
#include <vector>

class ResourceStrategy {
	private:
		int _foodEmergencyThreshold;
		int _foodComfortTarget;
		std::map<ResourceType, int> _stockTargets;

	public:
		explicit ResourceStrategy(
			int foodEmergencyThreshold = 6,
			int foodComfortTarget = 12
		);

		std::vector<ResourceType> buildPriority(const WorldState& state) const;

	private:
		int currentCount(const WorldState& state, ResourceType resource) const;
		std::vector<ResourceType> resourceDeficits(const WorldState& state) const;
};
