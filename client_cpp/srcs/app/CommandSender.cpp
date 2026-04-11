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
		Logger::debug("Sending PREND for resource: " + resource);
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

	// FIXED: Improved response matching with FIFO fallback
	void CommandSender::processResponse(const ServerMessage& msg) {
		if (msg.type != ServerMessageType::Response) return;
			// Ignore in_progress responses for incantation
			if (msg.status == "in_progress") {
				Logger::debug("Ignoring in_progress response for cmd: " + msg.cmd);
				return;
			}
		std::function<void(const ServerMessage&)> callback;
		uint64_t matchedId = 0;
		std::string matchedCmd;
		{
			std::lock_guard<std::mutex> lock(_mutex);
			
			Logger::debug("Processing response for cmd: " + msg.cmd + 
						  ", pending count=" + std::to_string(_pending.size()));
			
			for (const auto& p : _pending) {
				Logger::debug("  Pending: id=" + std::to_string(p.id) + ", cmd=" + p.cmd);
			}
			
			auto it = _pending.end();

			// Always match by cmd field if present
			if (!msg.cmd.empty()) {
				std::string expectedCmd = msg.cmd;
				if (!msg.arg.empty() && (msg.cmd == "prend" || msg.cmd == "pose")) {
					expectedCmd += " " + msg.arg;
				}
				it = std::find_if(_pending.begin(), _pending.end(),
					[&expectedCmd](const PendingCommand& p) { return p.cmd == expectedCmd; });

				// Removed the FIFO fallback for explicit commands to prevent grabbing wrong responses.
			} else if (!_pending.empty()) {
				// Fallback: cmd-less responses are assumed to answer the oldest pending command.
				it = _pending.begin();
			}

			if (it != _pending.end()) {
				matchedId = it->id;
				matchedCmd = it->cmd;
				callback = it->callback;
				_pending.erase(it);
				Logger::debug("Matched pending command: " + matchedCmd + " with id:" + std::to_string(matchedId));
			} else {
				Logger::warn("No matching pending command for: " + msg.cmd);
			}
		}

		if (callback) {
			ServerMessage callbackMsg = msg;
			if (callbackMsg.cmd.empty()) {
				callbackMsg.cmd = matchedCmd;
			}
			callback(callbackMsg);
		}
	}

	void CommandSender::checkTimeouts(int timeoutMs) {
		auto now = std::chrono::steady_clock::now();
		std::vector<std::pair<std::string, std::function<void(const ServerMessage&)>>> expired;

		{
			std::lock_guard<std::mutex> lock(_mutex);
			auto it = _pending.begin();
			while (it != _pending.end()) {
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->sentAt).count();
				if (elapsed >= timeoutMs) {
					expired.emplace_back(it->cmd, it->callback);
					it = _pending.erase(it);
				} else {
					++it;
				}
			}
		}

		for (const auto& entry : expired) {
			const std::string& cmd = entry.first;
			const auto& callback = entry.second;
			Logger::warn("Command timeout: " + cmd);

			if (callback) {
				ServerMessage timeoutMsg;
				timeoutMsg.type = ServerMessageType::Error;
				timeoutMsg.cmd = cmd;
				timeoutMsg.status = "timeout";
				callback(timeoutMsg);
			}
		}
	}

	void CommandSender::cancelAll() {
		std::lock_guard<std::mutex> lock(_mutex);
		_pending.clear();
	}
} // namespace zappy