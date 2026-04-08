#include "app/policy/IncantationStrategy.hpp"

#include "helpers/Logger.hpp"

IncantationStrategy::IncantationStrategy(int minFood, int minPlayersOnTile)
	: _minFood(minFood), _minPlayersOnTile(minPlayersOnTile) {
	_requiredResources[ResourceType::Linemate] = 1;
	_requiredResources[ResourceType::Deraumere] = 1;
	_requiredResources[ResourceType::Sibur] = 1;
}

IncantationAction IncantationStrategy::decide(const WorldState& state) const {
	const int food = state.inventoryCount(ResourceType::Nourriture).value_or(0);
	if (food < _minFood) {
		Logger::debug("IncantationStrategy: skip incantation, food=" + std::to_string(food)
			+ " < minFood=" + std::to_string(_minFood));
		return IncantationAction::None;
	}

	if (!hasRequiredResources(state)) {
		Logger::debug("IncantationStrategy: waiting for required tile resources before incantation");
		return IncantationAction::None;
	}

	const int playersOnTile = state.currentTilePlayerCount();
	if (playersOnTile < _minPlayersOnTile) {
		Logger::info("IncantationStrategy: requesting summon support, playersOnTile=" + std::to_string(playersOnTile)
			+ " < minPlayers=" + std::to_string(_minPlayersOnTile));
		return IncantationAction::Summon;
	}

	Logger::info("IncantationStrategy: incantation conditions met, food=" + std::to_string(food)
		+ " playersOnTile=" + std::to_string(playersOnTile));
	return IncantationAction::Incantate;
}

bool IncantationStrategy::hasRequiredResources(const WorldState& state) const {
	for (const auto& pair : _requiredResources) {
		if (!state.currentTileHasResource(pair.first)) {
			return false;
		}
	}
	return true;
}
