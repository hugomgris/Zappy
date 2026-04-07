#pragma once

#include "CommandType.hpp"

struct CommandSpec {
	int		timeoutMs = 3000;
	int		maxRetries = 0;
	bool	expectsReply = true;

	static CommandSpec forType(CommandType type) {
		switch (type) {
			case CommandType::Login: return {4000, 1, true};
			case CommandType::Avance: return {4000, 1, true};
			case CommandType::Droite: return {4000, 1, true};
			case CommandType::Gauche: return {4000, 1, true};
			case CommandType::Voir: return {8000, 1, true};
			case CommandType::Inventaire: return {4000, 1, true};
			case CommandType::Prend: return {4000, 2, true};
			case CommandType::Pose: return {4000, 1, true};
			case CommandType::Expulse: return {5000, 1, true};
			case CommandType::Broadcast: return {4000, 1, true};
			case CommandType::Incantation: return {120000, 0, true};
			case CommandType::Fork: return {10000, 1, true};
			case CommandType::ConnectNbr: return {2000, 1, true};
			default: return {3000, 0, true};
		}
	}
};