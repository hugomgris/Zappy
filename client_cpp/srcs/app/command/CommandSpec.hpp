#pragma once

#include "CommandType.hpp"

struct CommandSpec {
	int		timeoutMs = 3000;
	int		maxRetries = 0;
	bool	expectsReply = true;

	static CommandSpec forType(CommandType type) {
		switch (type) {
			case CommandType::Login: return {10000, 3, true};
			case CommandType::Avance: return {10000, 3, true};
			case CommandType::Droite: return {10000, 3, true};
			case CommandType::Gauche: return {10000, 3, true};
			case CommandType::Voir: return {15000, 3, true};
			case CommandType::Inventaire: return {10000, 3, true};
			case CommandType::Prend: return {10000, 4, true};
			case CommandType::Pose: return {10000, 3, true};
			case CommandType::Expulse: return {12000, 3, true};
			case CommandType::Broadcast: return {10000, 3, true};
			case CommandType::Incantation: return {120000, 0, true};
			case CommandType::Fork: return {15000, 3, true};
			case CommandType::ConnectNbr: return {5000, 3, true};
			default: return {3000, 0, true};
		}
	}
};