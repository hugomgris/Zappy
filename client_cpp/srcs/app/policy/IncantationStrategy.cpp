#include "app/policy/IncantationStrategy.hpp"

IncantationStrategy::IncantationStrategy(int minFood, int minPlayersOnTile)
	: _minFood(minFood), _minPlayersOnTile(minPlayersOnTile) {
	_requiredResources[ResourceType::Linemate] = 1;
	_requiredResources[ResourceType::Deraumere] = 1;
	_requiredResources[ResourceType::Sibur] = 1;
}

IncantationAction IncantationStrategy::decide(const WorldState& state) const {
	const int food = state.inventoryCount(ResourceType::Nourriture).value_or(0);
	if (food < _minFood) {
		return IncantationAction::None;
	}

	if (!hasRequiredResources(state)) {
		return IncantationAction::None;
	}

	if (state.currentTilePlayerCount() < _minPlayersOnTile) {
		return IncantationAction::Summon;
	}

	return IncantationAction::Incantate;
}

bool IncantationStrategy::hasRequiredResources(const WorldState& state) const {
	for (const auto& pair : _requiredResources) {
		const int current = state.inventoryCount(pair.first).value_or(0);
		if (current < pair.second) {
			return false;
		}
	}
	return true;
}
