#include "net/WebsocketClient.hpp"
#include "helpers/Logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cJSON.h>

static bool hasMessageType(const std::string& payload, const char* expectedType) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) return false;
    cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    bool matches = cJSON_IsString(type) && type->valuestring && std::string(type->valuestring) == expectedType;
    cJSON_Delete(root);
    return matches;
}

static bool isResponseFor(const std::string& payload, const char* expectedCmd) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) return false;
    cJSON* cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    bool matches = cJSON_IsString(cmd) && cmd->valuestring && std::string(cmd->valuestring) == expectedCmd;
    cJSON_Delete(root);
    return matches;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: ./test_prend <host> <port> <team_name>\n";
        return 1;
    }
    
    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    std::string teamName = argv[3];
    
    Logger::setLevel(LogLevel::Debug);
    
    zappy::WebsocketClient ws;
    
    // 1. Connect
    Logger::info("Connecting to " + host + ":" + std::to_string(port));
    zappy::Result res = ws.connect(host, port, true);
    if (!res.ok()) {
        Logger::error("Connect failed: " + res.message);
        return 1;
    }
    
    // 2. Wait for WebSocket connection
    int64_t now = 0;
    int attempts = 0;
    while (!ws.isConnected() && attempts < 500) {
        ws.tick(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        now += 10;
        attempts++;
    }
    
    if (!ws.isConnected()) {
        Logger::error("WebSocket connection timeout");
        return 1;
    }
    Logger::info("WebSocket connected!");
    
    // 3. Wait for initial "bienvenue" message
    Logger::info("Waiting for bienvenue...");
    bool gotBienvenue = false;
    
    for (int i = 0; i < 50 && !gotBienvenue; i++) {
        ws.tick(now);
        
        WebSocketFrame frame;
        IoResult io = ws.recvFrame(frame);
        if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            Logger::info("Received: " + text);
            
            if (hasMessageType(text, "bienvenue")) {
                gotBienvenue = true;
                Logger::info("Got bienvenue message!");
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        now += 100;
    }
    
    if (!gotBienvenue) {
        Logger::error("Never received bienvenue message");
        return 1;
    }
    
    // 4. Send proper JSON login
    cJSON* login = cJSON_CreateObject();
    cJSON_AddStringToObject(login, "type", "login");
    cJSON_AddStringToObject(login, "key", "SOME_KEY");
    cJSON_AddStringToObject(login, "role", "player");
    cJSON_AddStringToObject(login, "team-name", teamName.c_str());
    
    char* loginStr = cJSON_PrintUnformatted(login);
    std::string loginJson(loginStr);
    free(loginStr);
    cJSON_Delete(login);
    
    Logger::info("Sending login: " + loginJson);
    ws.sendText(loginJson);
    
    // 5. Wait for welcome response
    Logger::info("Waiting for welcome...");
    bool gotWelcome = false;
    
    for (int i = 0; i < 50 && !gotWelcome; i++) {
        ws.tick(now);
        
        WebSocketFrame frame;
        IoResult io = ws.recvFrame(frame);
        if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            Logger::info("Received: " + text);
            
            if (hasMessageType(text, "welcome")) {
                gotWelcome = true;
                Logger::info("Successfully logged in!");
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        now += 100;
    }
    
    if (!gotWelcome) {
        Logger::error("Never received welcome message");
        return 1;
    }
    
    // 6. Send voir to see what's around
    Logger::info("Sending voir command...");
    cJSON* voir = cJSON_CreateObject();
    cJSON_AddStringToObject(voir, "type", "cmd");
    cJSON_AddStringToObject(voir, "cmd", "voir");
    
    char* voirStr = cJSON_PrintUnformatted(voir);
    ws.sendText(voirStr);
    free(voirStr);
    cJSON_Delete(voir);
    
    // 7. Wait for voir response
    std::string voirResponse;
    for (int i = 0; i < 30; i++) {
        ws.tick(now);
        
        WebSocketFrame frame;
        IoResult io = ws.recvFrame(frame);
        if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            if (isResponseFor(text, "voir")) {
                voirResponse = text;
                Logger::info("Voir response received");
                break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        now += 100;
    }
    
    // 8. Test prend command
    Logger::info("\n=== TESTING PREND COMMAND ===");
    Logger::info("Sending prend nourriture...");
    
    cJSON* prend = cJSON_CreateObject();
    cJSON_AddStringToObject(prend, "type", "cmd");
    cJSON_AddStringToObject(prend, "cmd", "prend");
    cJSON_AddStringToObject(prend, "arg", "nourriture");
    
    char* prendStr = cJSON_PrintUnformatted(prend);
    std::string prendJson(prendStr);
    free(prendStr);
    cJSON_Delete(prend);
    
    auto startTime = std::chrono::steady_clock::now();
    ws.sendText(prendJson);
    
    // 9. Wait for prend response
    bool gotPrendResponse = false;
    for (int i = 0; i < 30; i++) {
        ws.tick(now);
        
        WebSocketFrame frame;
        IoResult io = ws.recvFrame(frame);
        if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            Logger::info("Received: " + text);
            
            if (isResponseFor(text, "prend")) {
                gotPrendResponse = true;
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime).count();
                Logger::info("✅ PREND response received in " + std::to_string(elapsed) + "ms");
                
                if (text.find("\"status\":\"ok\"") != std::string::npos) {
                    Logger::info("✅ PREND succeeded!");
                } else if (text.find("\"status\":\"ko\"") != std::string::npos) {
                    Logger::error("❌ PREND failed (status: ko)");
                }
                break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        now += 100;
    }
    
    if (!gotPrendResponse) {
        Logger::error("❌ No PREND response received after 3 seconds!");
    }
    
    // 10. Send inventory to verify
    Logger::info("\n=== VERIFYING INVENTORY ===");
    cJSON* inv = cJSON_CreateObject();
    cJSON_AddStringToObject(inv, "type", "cmd");
    cJSON_AddStringToObject(inv, "cmd", "inventaire");
    
    char* invStr = cJSON_PrintUnformatted(inv);
    ws.sendText(invStr);
    free(invStr);
    cJSON_Delete(inv);
    
    for (int i = 0; i < 10; i++) {
        ws.tick(now);
        
        WebSocketFrame frame;
        IoResult io = ws.recvFrame(frame);
        if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            if (isResponseFor(text, "inventaire")) {
                Logger::info("Inventory response: " + text);
                break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        now += 100;
    }
    
    ws.close();
    Logger::info("\n=== TEST COMPLETE ===");
    return gotPrendResponse ? 0 : 1;
}