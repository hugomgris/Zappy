#include "app/CommandSender.hpp"

#include "../helpers/Logger.hpp"
#include "net/WebsocketClient.hpp"

CommandSender::CommandSender(WebsocketClient& ws) : _ws(ws) {}

Result CommandSender::sendLogin(const Arguments& args) {
	const std::string payload = std::string("{\"type\":\"login\",\"key\":\"SOME_KEY\",\"role\":\"player\",\"team-name\":\"")
		+ args.teamName + "\"}";
	return sendRawJson(payload, "login");
}

Result CommandSender::sendVoir() {
	return sendCommand("voir");
}

Result CommandSender::sendInventaire() {
	return sendCommand("inventaire");
}

Result CommandSender::sendAvance() {
	return sendCommand("avance");
}

Result CommandSender::sendDroite() {
	return sendCommand("droite");
}

Result CommandSender::sendGauche() {
	return sendCommand("gauche");
}

Result CommandSender::sendPrend(ResourceType resource) {
	return sendCommand("prend", toProtocolString(resource));
}

Result CommandSender::sendPose(ResourceType resource) {
	return sendCommand("pose", toProtocolString(resource));
}

Result CommandSender::sendExpulse() {
	return sendCommand("expulse");
}

Result CommandSender::sendBroadcast(const std::string& message) {
	return sendCommand("broadcast", message);
}

Result CommandSender::sendIncantation() {
	return sendCommand("incantation");
}

Result CommandSender::sendFork() {
	return sendCommand("fork");
}

Result CommandSender::sendConnectNbr() {
	return sendCommand("connect_nbr");
}

Result CommandSender::sendPrendNourriture() {
	return sendPrend(ResourceType::Nourriture);
}

Result CommandSender::sendCommand(const std::string& cmd, const std::string& arg) {
	std::string payload = std::string("{\"type\":\"cmd\",\"cmd\":\"") + cmd + "\"";
	if (!arg.empty()) {
		payload += std::string(",\"arg\":\"") + arg + "\"";
	}
	payload += "}";
	return sendRawJson(payload, "command '" + cmd + "'");
}

Result CommandSender::sendRawJson(const std::string& payload, const std::string& context) {
	const IoResult sendRes = _ws.sendText(payload);
	if (sendRes.status != NetStatus::Ok) {
		return Result::failure(ErrorCode::NetworkError, "Failed to queue " + context + " frame: " + sendRes.message);
	}
	return Result::success();
}
