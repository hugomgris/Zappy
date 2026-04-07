#pragma once

enum class ResourceType {
	Nourriture = 0,
	Linemate,
	Deraumere,
	Sibur,
	Mendiane,
	Phiras,
	Thystame
};

inline const char* toProtocolString(ResourceType resource) {
	switch (resource) {
		case ResourceType::Nourriture: return "nourriture";
		case ResourceType::Linemate: return "linemate";
		case ResourceType::Deraumere: return "deraumere";
		case ResourceType::Sibur: return "sibur";
		case ResourceType::Mendiane: return "mendiane";
		case ResourceType::Phiras: return "phiras";
		case ResourceType::Thystame: return "thystame";
		default: return "nourriture";
	}
}