#pragma once

#include "CommandType.hpp"

#include <cstdlib>

struct CommandSpec {
	int		timeoutMs = 3000;
	int		maxRetries = 0;
	bool	expectsReply = true;

	static int resolvedServerTimeUnitMs() {
		const char* value = std::getenv("ZAPPY_TIME_UNIT");
		if (!value || !*value) {
			return 126;
		}

		char* end = nullptr;
		const long parsed = std::strtol(value, &end, 10);
		if (end == value || (end && *end != '\0') || parsed < 1 || parsed > 5000) {
			return 126;
		}
		return static_cast<int>(parsed);
	}

	static int scaleMs(int baselineMs) {
		long scaled = (static_cast<long>(baselineMs) * resolvedServerTimeUnitMs()) / 126;
		if (scaled < 1000) {
			scaled = 1000;
		}
		if (scaled > 600000) {
			scaled = 600000;
		}
		return static_cast<int>(scaled);
	}

	static CommandSpec forType(CommandType type) {
		switch (type) {
			case CommandType::Login: return {scaleMs(10000), 3, true};
			case CommandType::Avance: return {scaleMs(10000), 3, true};
			case CommandType::Droite: return {scaleMs(10000), 3, true};
			case CommandType::Gauche: return {scaleMs(10000), 3, true};
			case CommandType::Voir: return {scaleMs(15000), 3, true};
			case CommandType::Inventaire: return {scaleMs(10000), 3, true};
			case CommandType::Prend: return {scaleMs(10000), 4, true};
			case CommandType::Pose: return {scaleMs(10000), 3, true};
			case CommandType::Expulse: return {scaleMs(12000), 3, true};
			case CommandType::Broadcast: return {scaleMs(10000), 3, true};
			case CommandType::Incantation: return {scaleMs(120000), 0, true};
			case CommandType::Fork: return {scaleMs(15000), 3, true};
			case CommandType::ConnectNbr: return {scaleMs(5000), 3, true};
			default: return {scaleMs(3000), 0, true};
		}
	}
};