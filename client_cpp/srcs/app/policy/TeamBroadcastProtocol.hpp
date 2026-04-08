#pragma once

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
};

class TeamBroadcastProtocol {
	public:
		static TeamSignal parse(const std::string& message);
};
