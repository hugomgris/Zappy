#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
#include <vector>

#include "../../srcs/agent/Behavior.hpp"

using namespace testing;

class FakeSenderDirectionUpdate : public Sender {
    public:
        std::string lastCmd;
        std::function<void(const ServerMessage&)> lastCallback;

        FakeSenderDirectionUpdate(WebsocketClient& ws) : Sender(ws) {}

        Result sendVoir() override       { lastCmd = "voir";       return Result::success(); }
        Result sendInventaire() override { lastCmd = "inventaire"; return Result::success(); }
        Result sendGauche() override     { lastCmd = "gauche";     return Result::success(); }
        Result sendDroite() override     { lastCmd = "droite";     return Result::success(); }
        Result sendAvance() override     { lastCmd = "avance";     return Result::success(); }
        Result sendPrend(const std::string& resource) override {
            lastCmd = "prend " + resource;
            return Result::success();
        }

        void expect(const std::string&,
                    std::function<void(const ServerMessage&)> cb) override {
            lastCallback = cb;
        }

        void fireCallback(const ServerMessage& msg) {
            if (lastCallback) lastCallback(msg);
        }
};

class MockWebsocketClientDirectionUpdate : public WebsocketClient {
    public:
        MOCK_METHOD(IoResult, sendText, (const std::string& text), (override));
};

class BehaviorDirectionUpdateTest : public ::testing::Test {
    protected:
        MockWebsocketClientDirectionUpdate ws;
        FakeSenderDirectionUpdate          sender{ws};
        WorldState                         state;
        Behavior                           behavior{sender, state};

        void SetUp() override {
            state.player.inventory.nourriture = 100;
        }

        VisionTile makeTile(int localX, int localY, std::vector<std::string> items) {
            VisionTile tile;
            tile.distance = std::max(std::abs(localX), std::abs(localY));
            tile.localX = localX;
            tile.localY = localY;
            for (const auto& item : items) {
                if (item == "player") tile.playerCount++;
                else tile.items.push_back(item);
            }
            return tile;
        }

        void giveFreshVision(std::vector<VisionTile> tiles) {
            state.vision = tiles;
            behavior.setVisionStale();
            behavior.tick(0); // sends voir
            
            ServerMessage msg;
            msg.type = MsgType::Response;
            msg.cmd = "voir";
            msg.status = "ok";
            msg.vision = tiles;
            sender.fireCallback(msg);
            
            sender.lastCmd = "";
        }

        void giveFreshInventory(Inventory inv) {
            state.player.inventory = inv;
            behavior.refreshInventory();
            
            ServerMessage msg;
            msg.type = MsgType::Response;
            msg.cmd = "inventaire";
            msg.status = "ok";
            msg.inventory = inv;
            sender.fireCallback(msg);

            sender.lastCmd = "";
        }
};

TEST_F(BehaviorDirectionUpdateTest, RallyNavPlanClearedOnDirectionUpdate) {
    // Setup: follower is in MovingToRally with direction=3 (right)
    state.player.level = 2;
    state.player.inventory = { 100,1,1,1,0,0,0 };  // all stones for level 2, and plenty of food

    // Get into MovingToRally
    giveFreshVision({ makeTile(0,0,{}) });
    giveFreshInventory(state.player.inventory);
    
    // Force state
    behavior.setAIState(AIState::MovingToRally);

    // First RALLY → direction 3
    ServerMessage rally3;
    rally3.type = MsgType::Broadcast;
    rally3.messageText = "RALLY:2";
    rally3.broadcastDirection = 3;
    behavior.onBroadcast(rally3);

    // Tick → generates a nav plan (turn right + forward)
    giveFreshVision({ makeTile(0,0,{}) });
    behavior.tick(1000);
    // nav plan is now non-empty (has TurnRight as the first command)

    // Complete the first command so the agent isn't blocked
    ServerMessage droiteOk;
    droiteOk.type = MsgType::Response;
    droiteOk.cmd = "droite";
    droiteOk.status = "ok";
    sender.fireCallback(droiteOk);

    // Second RALLY → direction 1 (straight ahead, different)
    ServerMessage rally1;
    rally1.type = MsgType::Broadcast;
    rally1.messageText = "RALLY:2";
    rally1.broadcastDirection = 1;
    behavior.onBroadcast(rally1);

    // Nav plan should be cleared — next tick will recompute for direction 1
    // The previous movement made the vision stale, so refresh it manually:
    giveFreshVision({ makeTile(0,0,{}) });

    // So the command queue won't execute the old TurnRight, but instead recompute
    sender.lastCmd = "";
    behavior.tick(1100);
    
    // First command for direction 1 is Forward (no turns needed)
    EXPECT_EQ(sender.lastCmd, "avance");
}
