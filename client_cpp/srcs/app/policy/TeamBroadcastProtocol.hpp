#pragma once

#include <optional>
#include <string>

enum class TeamSignalKind {
	Unknown = 0,
	NeedPlayers,
	NeedFood,
	OfferFood,
	RoleScout,
	RoleGatherer,
	RoleAssembler,
	OnMyWay,
};

struct TeamSignal {
	TeamSignalKind kind = TeamSignalKind::Unknown;
	std::string rawMessage;
	std::optional<int> direction;
};

class TeamBroadcastProtocol {
	public:
		static TeamSignal parse(const std::string& message);
};
