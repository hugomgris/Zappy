#pragma once

#include <gtest/gtest.h>
#include "app/WorldState.hpp"
#include "app/CommandSender.hpp"
#include "../mocks/mock_websocket_client.hpp"

namespace zappy {
namespace testing {

class WorldStateFixture : public ::testing::Test {
protected:
    void SetUp() override {
        state = std::make_unique<WorldState>();
        
        // Setup welcome
        ServerMessage welcome;
        welcome.type = ServerMessageType::Welcome;
        welcome.remainingClients = 5;
        welcome.mapSize = MapSize{10, 10};
        state->onWelcome(welcome);
    }
    
    void setupInventory(const std::map<std::string, int>& inv) {
        ServerMessage msg;
        msg.type = ServerMessageType::Response;
        msg.cmd = "inventaire";
        msg.inventory = inv;
        state->onResponse(msg);
    }
    
    void setupVision(const std::vector<VisionTile>& vision) {
        ServerMessage msg;
        msg.type = ServerMessageType::Response;
        msg.cmd = "voir";
        msg.vision = vision;
        state->onResponse(msg);
    }
    
    std::unique_ptr<WorldState> state;
};

class CommandSenderFixture : public ::testing::Test {
protected:
    void SetUp() override {
        mock_ws = std::make_unique<MockWebsocketClient>();
        sender = std::make_unique<CommandSender>(*mock_ws);
        
        // Default expectation - most tests will override
        ON_CALL(*mock_ws, sendText(::testing::_))
            .WillByDefault([](const std::string&) {
                IoResult res;
                res.status = NetStatus::Ok;
                return res;
            });
    }
    
    std::unique_ptr<MockWebsocketClient> mock_ws;
    std::unique_ptr<CommandSender> sender;
};

} // namespace testing
} // namespace zappy