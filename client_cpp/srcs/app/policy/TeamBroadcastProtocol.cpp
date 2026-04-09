#include "app/policy/TeamBroadcastProtocol.hpp"

#include <cctype>

namespace {
	std::optional<std::string> extractJsonField(const std::string& text, const std::string& fieldName) {
		const std::string keyToken = "\"" + fieldName + "\"";
		const std::size_t keyPos = text.find(keyToken);
		if (keyPos == std::string::npos) {
			return std::nullopt;
		}

		const std::size_t colonPos = text.find(':', keyPos + keyToken.size());
		if (colonPos == std::string::npos) {
			return std::nullopt;
		}

		std::size_t valuePos = colonPos + 1;
		while (valuePos < text.size() && std::isspace(static_cast<unsigned char>(text[valuePos]))) {
			++valuePos;
		}

		if (valuePos >= text.size() || text[valuePos] != '"') {
			return std::nullopt;
		}

		++valuePos;
		std::size_t closePos = valuePos;
		while (closePos < text.size()) {
			if (text[closePos] == '"' && (closePos == valuePos || text[closePos - 1] != '\\')) {
				return text.substr(valuePos, closePos - valuePos);
			}
			++closePos;
		}

		return std::nullopt;
	}

	std::optional<int> parseDirection(const std::string& text) {
		for (std::size_t index = 0; index < text.size(); ++index) {
			const char current = text[index];
			if (current == '-' || std::isdigit(static_cast<unsigned char>(current))) {
				std::size_t end = index + 1;
				while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
					++end;
				}
				try {
					return std::stoi(text.substr(index, end - index));
				} catch (...) {
					return std::nullopt;
				}
			}
		}

		return std::nullopt;
	}
}

TeamSignal TeamBroadcastProtocol::parse(const std::string& message) {
	TeamSignal signal;
	std::string payload = message;

	const std::optional<std::string> cmd = extractJsonField(message, "cmd");
	const std::optional<std::string> type = extractJsonField(message, "type");
	if ((cmd.has_value() && *cmd == "message") || (type.has_value() && *type == "message")) {
		const std::optional<std::string> arg = extractJsonField(message, "arg");
		const std::optional<std::string> status = extractJsonField(message, "status");
		if (arg.has_value()) {
			payload = *arg;
		}
		if (status.has_value()) {
			signal.direction = parseDirection(*status);
		}
	}

	signal.rawMessage = payload;

	if (payload == "need_players_for_incantation" || payload == "team:need:players") {
		signal.kind = TeamSignalKind::NeedPlayers;
		return signal;
	}

	if (payload == "team:need:food") {
		signal.kind = TeamSignalKind::NeedFood;
		return signal;
	}

	if (payload == "team:offer:food") {
		signal.kind = TeamSignalKind::OfferFood;
		return signal;
	}

	if (payload == "team:role:scout") {
		signal.kind = TeamSignalKind::RoleScout;
		return signal;
	}

	if (payload == "team:role:gatherer") {
		signal.kind = TeamSignalKind::RoleGatherer;
		return signal;
	}

	if (payload == "team:role:assembler") {
		signal.kind = TeamSignalKind::RoleAssembler;
		return signal;
	}

	if (payload == "team:on_my_way") {
		signal.kind = TeamSignalKind::OnMyWay;
		return signal;
	}

	return signal;
}
