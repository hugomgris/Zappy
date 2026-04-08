#pragma once

#include "app/command/ResourceType.hpp"
#include "app/state/WorldState.hpp"

#include <map>

enum class IncantationAction {
	None = 0,
	Summon,
	Incantate,
};

class IncantationStrategy {
	private:
		int _minFood;
		int _minPlayersOnTile;
		std::map<ResourceType, int> _requiredResources;

	public:
		explicit IncantationStrategy(int minFood = 0, int minPlayersOnTile = 0);

		IncantationAction decide(const WorldState& state) const;

	private:
		bool hasRequiredResources(const WorldState& state) const;
};
