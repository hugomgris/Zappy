#include "net/WebsocketClient.hpp"
#include "helpers/Logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cJSON.h>  // You'll need to link with cJSON

static bool hasMessageType(const std::string& payload, const char* expectedType) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return false;
    }

    cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    bool matches = cJSON_IsString(type) && type->valuestring && std::string(type->valuestring) == expectedType;

    cJSON_Delete(root);
    return matches;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: ./simple_client <host> <port> <team_name>\n";
        return 1;
    }
    
    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    std::string teamName = argv[3];
    
    Logger::setLevel(LogLevel::Debug);
    
    WebsocketClient ws;
    
    // 1. Connect
    Logger::info("Connecting to " + host + ":" + std::to_string(port));
    Result res = ws.connect(host, port, true);
    if (!res.ok()) {
        Logger::error("Connect failed: " + res.message);
        return 1;
    }
    
    // 2. Wait for WebSocket connection
    int64_t now = 0;
    while (!ws.isConnected()) {
        ws.tick(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        now += 10;
        if (now > 5000) {
            Logger::error("WebSocket connection timeout");
            return 1;
        }
    }
    Logger::info("WebSocket connected!");
    
    // 3. Wait for initial "bienvenue" message
    Logger::info("Waiting for bienvenue...");
    std::string bienvenue;
    bool gotBienvenue = false;
    
    for (int i = 0; i < 50 && !gotBienvenue; i++) {
        ws.tick(now);
        
        WebSocketFrame frame;
        IoResult io = ws.recvFrame(frame);
        if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            Logger::info("Received: " + text);
            
            if (hasMessageType(text, "bienvenue")) {
                bienvenue = text;
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
                
                // Parse map size if available
                if (text.find("map_size") != std::string::npos) {
                    Logger::info("Map size information received");
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        now += 100;
    }
    
    if (!gotWelcome) {
        Logger::error("Never received welcome message");
        return 1;
    }
    
    // 6. Test a simple command
    Logger::info("Sending voir command...");
    cJSON* cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "type", "cmd");
    cJSON_AddStringToObject(cmd, "cmd", "voir");
    
    char* cmdStr = cJSON_PrintUnformatted(cmd);
    std::string cmdJson(cmdStr);
    free(cmdStr);
    cJSON_Delete(cmd);
    
    ws.sendText(cmdJson);
    
    // 7. Wait for response
    for (int i = 0; i < 30; i++) {
        ws.tick(now);
        
        WebSocketFrame frame;
        IoResult io = ws.recvFrame(frame);
        if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            Logger::info("Response: " + text);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        now += 100;
    }
    
    ws.close();
    Logger::info("Test completed successfully!");
    return 0;
}