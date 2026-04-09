#include "app/policy/IncantationStrategy.hpp"

#include "helpers/Logger.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
	bool easyAscensionModeEnabled() {
		const char* mode = std::getenv("ZAPPY_EASY_ASCENSION");
		return mode != nullptr && std::strcmp(mode, "1") == 0;
	}
}

IncantationStrategy::IncantationStrategy(int minFood, int minPlayersOnTile)
	: _minFood(minFood), _minPlayersOnTile(minPlayersOnTile) {}

IncantationAction IncantationStrategy::decide(const WorldState& state) const {
	const int food = state.inventoryCount(ResourceType::Nourriture).value_or(0);
	const int playerLevel = state.playerLevel();
	if (playerLevel >= 8) {
		return IncantationAction::None;
	}

	if (easyAscensionModeEnabled()) {
		Logger::info("IncantationStrategy: easy ascension mode active, requesting incantation");
		return IncantationAction::Incantate;
	}

	if (food < _minFood) {
		Logger::debug("IncantationStrategy: skip incantation, food=" + std::to_string(food)
			+ " < minFood=" + std::to_string(_minFood));
		return IncantationAction::None;
	}

	if (!hasRequiredResources(state, playerLevel)) {
		Logger::debug("IncantationStrategy: waiting for required tile resources before incantation");
		return IncantationAction::None;
	}

	const int playersOnTile = state.currentTilePlayerCount();
	const int requiredPlayers = std::max(requiredPlayersForLevel(playerLevel), _minPlayersOnTile);
	if (playersOnTile < requiredPlayers) {
		Logger::info("IncantationStrategy: requesting summon support, playersOnTile=" + std::to_string(playersOnTile)
			+ " < minPlayers=" + std::to_string(requiredPlayers));
		return IncantationAction::Summon;
	}

	Logger::info("IncantationStrategy: incantation conditions met, food=" + std::to_string(food)
		+ " playersOnTile=" + std::to_string(playersOnTile)
		+ " level=" + std::to_string(playerLevel));
	return IncantationAction::Incantate;
}

bool IncantationStrategy::hasRequiredResources(const WorldState& state, int playerLevel) const {
	const std::map<ResourceType, int> required = requiredResourcesForLevel(playerLevel);
	for (const auto& pair : required) {
		if (state.currentTileResourceCount(pair.first) < pair.second) {
			return false;
		}
	}
	return true;
}

int IncantationStrategy::requiredPlayersForLevel(int playerLevel) const {
	switch (playerLevel) {
		case 1: return 1;
		case 2: return 2;
		case 3: return 2;
		case 4: return 4;
		case 5: return 4;
		case 6: return 6;
		case 7: return 6;
		default: return 1;
	}
}

std::map<ResourceType, int> IncantationStrategy::requiredResourcesForLevel(int playerLevel) const {
	switch (playerLevel) {
		case 1:
			return {
				{ResourceType::Linemate, 1},
			};
		case 2:
			return {
				{ResourceType::Linemate, 1},
				{ResourceType::Deraumere, 1},
				{ResourceType::Sibur, 1},
			};
		case 3:
			return {
				{ResourceType::Linemate, 2},
				{ResourceType::Sibur, 1},
				{ResourceType::Phiras, 2},
			};
		case 4:
			return {
				{ResourceType::Linemate, 1},
				{ResourceType::Deraumere, 1},
				{ResourceType::Sibur, 2},
				{ResourceType::Phiras, 1},
			};
		case 5:
			return {
				{ResourceType::Linemate, 1},
				{ResourceType::Deraumere, 2},
				{ResourceType::Sibur, 1},
				{ResourceType::Mendiane, 3},
			};
		case 6:
			return {
				{ResourceType::Linemate, 1},
				{ResourceType::Deraumere, 2},
				{ResourceType::Sibur, 3},
				{ResourceType::Phiras, 1},
			};
		case 7:
			return {
				{ResourceType::Linemate, 2},
				{ResourceType::Deraumere, 2},
				{ResourceType::Sibur, 2},
				{ResourceType::Mendiane, 2},
				{ResourceType::Phiras, 2},
				{ResourceType::Thystame, 1},
			};
		default:
			return {};
	}
}
