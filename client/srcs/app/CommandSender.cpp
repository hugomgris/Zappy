/*
 * CommandSender.cpp
 *
 * No new functional changes vs the previous rewrite. The fixes that matter
 * for the "Buffer full!" problem live in AI.cpp (_commandInFlight serialization).
 * CommandSender itself is correct — it faithfully sends and tracks pending
 * commands. The problem was the AI sending too many commands at once.
 *
 * What's kept from the previous rewrite:
 * 1. expectResponse key for prend/pose always uses "prend <resource>" / "pose <resource>".
 * 2. processResponse forwards "in_progress" for incantation without removing it.
 * 3. FIFO fallback for cmd-less server replies is documented.
 * 4. checkTimeouts takes an explicit timeoutMs.
 */

#include "CommandSender.hpp"
#include "../helpers/Logger.hpp"
#include <algorithm>

namespace zappy {

CommandSender::CommandSender(WebsocketClient& ws) : _ws(ws) {}

// ---------------------------------------------------------------------------
// raw sending
// ---------------------------------------------------------------------------

Result CommandSender::sendRaw(const std::string& json) {
    Logger::debug("TX: " + json);
    IoResult res = _ws.sendText(json);
    if (res.status != NetStatus::Ok)
        return Result::failure(ErrorCode::NetworkError, res.message);
    return Result::success();
}

Result CommandSender::sendCommandObj(cJSON* cmd) {
    char* str = cJSON_PrintUnformatted(cmd);
    std::string json(str);
    free(str);
    cJSON_Delete(cmd);
    return sendRaw(json);
}

// ---------------------------------------------------------------------------
// login
// ---------------------------------------------------------------------------

Result CommandSender::sendLogin(const std::string& teamName, const std::string& key) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "login");
    cJSON_AddStringToObject(obj, "key",  key.c_str());
    cJSON_AddStringToObject(obj, "role", "player");
    cJSON_AddStringToObject(obj, "team-name", teamName.c_str());
    return sendCommandObj(obj);
}

// ---------------------------------------------------------------------------
// movement
// ---------------------------------------------------------------------------

Result CommandSender::sendAvance() {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "avance");
    return sendCommandObj(obj);
}

Result CommandSender::sendDroite() {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "droite");
    return sendCommandObj(obj);
}

Result CommandSender::sendGauche() {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "gauche");
    return sendCommandObj(obj);
}

// ---------------------------------------------------------------------------
// info
// ---------------------------------------------------------------------------

Result CommandSender::sendVoir() {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "voir");
    return sendCommandObj(obj);
}

Result CommandSender::sendInventaire() {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "inventaire");
    return sendCommandObj(obj);
}

Result CommandSender::sendConnectNbr() {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "connect_nbr");
    return sendCommandObj(obj);
}

// ---------------------------------------------------------------------------
// inventory actions
// ---------------------------------------------------------------------------

Result CommandSender::sendPrend(const std::string& resource) {
    Logger::debug("CommandSender: sendPrend(" + resource + ")");
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "prend");
    cJSON_AddStringToObject(obj, "arg",  resource.c_str());
    return sendCommandObj(obj);
}

Result CommandSender::sendPose(const std::string& resource) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "pose");
    cJSON_AddStringToObject(obj, "arg",  resource.c_str());
    return sendCommandObj(obj);
}

// ---------------------------------------------------------------------------
// social
// ---------------------------------------------------------------------------

Result CommandSender::sendExpulse() {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "expulse");
    return sendCommandObj(obj);
}

Result CommandSender::sendBroadcast(const std::string& msg) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "broadcast");
    cJSON_AddStringToObject(obj, "arg",  msg.c_str());
    return sendCommandObj(obj);
}

// ---------------------------------------------------------------------------
// progression
// ---------------------------------------------------------------------------

Result CommandSender::sendIncantation() {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "incantation");
    return sendCommandObj(obj);
}

Result CommandSender::sendFork() {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", "cmd");
    cJSON_AddStringToObject(obj, "cmd",  "fork");
    return sendCommandObj(obj);
}

// ---------------------------------------------------------------------------
// response tracking
// ---------------------------------------------------------------------------

uint64_t CommandSender::expectResponse(
    const std::string& cmd,
    std::function<void(const ServerMessage&)> cb)
{
    std::lock_guard<std::mutex> lock(_mutex);
    uint64_t id = _nextId++;
    _pending.push_back({id, cmd, std::chrono::steady_clock::now(), cb});
    Logger::debug("CommandSender: expectResponse key='" + cmd +
                  "' id=" + std::to_string(id));
    return id;
}

void CommandSender::processResponse(const ServerMessage& msg) {
    if (msg.type != ServerMessageType::Response) return;

    // Build lookup key. For prend/pose the key includes the resource arg.
    std::string lookupKey = msg.cmd;
    if (!msg.arg.empty() && (msg.cmd == "prend" || msg.cmd == "pose"))
        lookupKey = msg.cmd + " " + msg.arg;

    // Incantation "in_progress": forward to callback but keep it in pending
    // so we can still receive the final ok/ko.
    if (msg.status == "in_progress" && msg.cmd == "incantation") {
        Logger::debug("CommandSender: incantation in_progress, forwarding but keeping pending");
        std::function<void(const ServerMessage&)> cb;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = std::find_if(_pending.begin(), _pending.end(),
                [](const PendingCommand& p) { return p.cmd == "incantation"; });
            if (it != _pending.end()) cb = it->callback;
        }
        if (cb) cb(msg);
        return;
    }

    // Any other "in_progress" is unexpected — ignore
    if (msg.status == "in_progress") {
        Logger::debug("CommandSender: ignoring in_progress for " + msg.cmd);
        return;
    }

    std::function<void(const ServerMessage&)> callback;
    std::string matchedCmd;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        auto it = _pending.end();

        if (!lookupKey.empty()) {
            it = std::find_if(_pending.begin(), _pending.end(),
                [&](const PendingCommand& p) { return p.cmd == lookupKey; });
        }

        // FIFO fallback: if the server reply carries no cmd field
        if (it == _pending.end() && msg.cmd.empty() && !_pending.empty()) {
            it = _pending.begin();
            Logger::debug("CommandSender: FIFO fallback for cmd-less response");
        }

        if (it != _pending.end()) {
            matchedCmd = it->cmd;
            callback   = it->callback;
            _pending.erase(it);
            Logger::debug("CommandSender: matched pending '" + matchedCmd + "'");
        } else {
            Logger::warn("CommandSender: no pending command for key='" +
                         lookupKey + "' (msg.cmd='" + msg.cmd + "')");
        }
    }

    if (callback) {
        ServerMessage out = msg;
        if (out.cmd.empty()) out.cmd = matchedCmd;
        callback(out);
    }
}

void CommandSender::checkTimeouts(int timeoutMs) {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::pair<std::string, std::function<void(const ServerMessage&)>>> expired;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto it = _pending.begin(); it != _pending.end(); ) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->sentAt).count();
            if (elapsed >= timeoutMs) {
                Logger::warn("CommandSender: timeout on '" + it->cmd + "'");
                expired.emplace_back(it->cmd, it->callback);
                it = _pending.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (const auto& [cmd, cb] : expired) {
        if (cb) {
            ServerMessage t;
            t.type   = ServerMessageType::Error;
            t.cmd    = cmd;
            t.status = "timeout";
            cb(t);
        }
    }
}

void CommandSender::cancelAll() {
    std::lock_guard<std::mutex> lock(_mutex);
    _pending.clear();
}

} // namespace zappy
