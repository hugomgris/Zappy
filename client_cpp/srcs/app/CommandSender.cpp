#include "CommandSender.hpp"
#include "helpers/Logger.hpp"
#include <algorithm>

namespace zappy {
	CommandSender::CommandSender(WebsocketClient& ws) : _ws(ws) {}

	Result CommandSender::sendRaw(const std::string& json) {
		Logger::debug("TX: " + json);
		IoResult res = _ws.sendText(json);
		if (res.status != NetStatus::Ok) {
			return Result::failure(ErrorCode::NetworkError, res.message);
		}
		return Result::success();
	}

	Result CommandSender::sendCommandObj(cJSON* cmd) {
		char* str = cJSON_PrintUnformatted(cmd);
		std::string json(str);
		free(str);
		cJSON_Delete(cmd);
		return sendRaw(json);
	}

	Result CommandSender::sendLogin(const std::string& teamName, const std::string& key) {
		cJSON* login = cJSON_CreateObject();
		cJSON_AddStringToObject(login, "type", "login");
		cJSON_AddStringToObject(login, "key", key.c_str());
		cJSON_AddStringToObject(login, "role", "player");
		cJSON_AddStringToObject(login, "team-name", teamName.c_str());
		return sendCommandObj(login);
	}

	Result CommandSender::sendVoir() {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "voir");
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendInventaire() {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "inventaire");
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendAvance() {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "avance");
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendDroite() {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "droite");
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendGauche() {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "gauche");
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendPrend(const std::string& resource) {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "prend");
		cJSON_AddStringToObject(cmd, "arg", resource.c_str());
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendPose(const std::string& resource) {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "pose");
		cJSON_AddStringToObject(cmd, "arg", resource.c_str());
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendExpulse() {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "expulse");
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendBroadcast(const std::string& msg) {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "broadcast");
		cJSON_AddStringToObject(cmd, "arg", msg.c_str());
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendIncantation() {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "incantation");
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendFork() {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "fork");
		return sendCommandObj(cmd);
	}

	Result CommandSender::sendConnectNbr() {
		cJSON* cmd = cJSON_CreateObject();
		cJSON_AddStringToObject(cmd, "type", "cmd");
		cJSON_AddStringToObject(cmd, "cmd", "connect_nbr");
		return sendCommandObj(cmd);
	}

	uint64_t CommandSender::expectResponse(const std::string& cmd, std::function<void(const ServerMessage&)> cb) {
		std::lock_guard<std::mutex> lock(_mutex);
		uint64_t id = _nextId++;
		_pending.push_back({id, cmd, std::chrono::steady_clock::now(), cb});
		Logger::debug("Expecting response for: " + cmd + " (id=" + std::to_string(id) + ")");
		return id;
	}

	// FIXED: Search entire queue for matching command, not just front
	void CommandSender::processResponse(const ServerMessage& msg) {
		if (msg.type != ServerMessageType::Response) return;

		std::lock_guard<std::mutex> lock(_mutex);
		
		// Find matching pending command
		auto it = std::find_if(_pending.begin(), _pending.end(),
			[&msg](const PendingCommand& p) { return p.cmd == msg.cmd; });
		
		if (it != _pending.end()) {
			Logger::debug("Matched response for: " + msg.cmd + " (id=" + std::to_string(it->id) + ")");
			if (it->callback) it->callback(msg);
			_pending.erase(it);
		} else {
			Logger::debug("No pending command found for response: " + msg.cmd);
		}
	}

	// FIXED: Invoke callback on timeout with error message
	void CommandSender::checkTimeouts(int timeoutMs) {
		std::lock_guard<std::mutex> lock(_mutex);
		auto now = std::chrono::steady_clock::now();
		
		auto it = _pending.begin();
		while (it != _pending.end()) {
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->sentAt).count();
			if (elapsed >= timeoutMs) {
				Logger::warn("Command timeout: " + it->cmd + " (id=" + std::to_string(it->id) + ")");
				
				// Create timeout message and invoke callback
				ServerMessage timeoutMsg;
				timeoutMsg.type = ServerMessageType::Error;
				timeoutMsg.cmd = it->cmd;
				timeoutMsg.status = "timeout";
				
				if (it->callback) {
					it->callback(timeoutMsg);
				}
				
				it = _pending.erase(it);
			} else {
				++it;
			}
		}
	}

	void CommandSender::cancelAll() {
		std::lock_guard<std::mutex> lock(_mutex);
		_pending.clear();
	}
} // namespace zappy