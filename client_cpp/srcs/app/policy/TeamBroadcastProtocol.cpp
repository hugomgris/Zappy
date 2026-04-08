#include "app/policy/TeamBroadcastProtocol.hpp"

TeamSignal TeamBroadcastProtocol::parse(const std::string& message) {
	TeamSignal signal;
	signal.rawMessage = message;

	if (message == "need_players_for_incantation" || message == "team:need:players") {
		signal.kind = TeamSignalKind::NeedPlayers;
		return signal;
	}

	if (message == "team:need:food") {
		signal.kind = TeamSignalKind::NeedFood;
		return signal;
	}

	if (message == "team:offer:food") {
		signal.kind = TeamSignalKind::OfferFood;
		return signal;
	}

	if (message == "team:role:scout") {
		signal.kind = TeamSignalKind::RoleScout;
		return signal;
	}

	if (message == "team:role:gatherer") {
		signal.kind = TeamSignalKind::RoleGatherer;
		return signal;
	}

	if (message == "team:role:assembler") {
		signal.kind = TeamSignalKind::RoleAssembler;
		return signal;
	}

	if (message == "team:on_my_way") {
		signal.kind = TeamSignalKind::OnMyWay;
		return signal;
	}

	return signal;
}
