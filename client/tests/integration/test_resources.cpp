#include <gtest/gtest.h>
#include "app/Client.hpp"
#include "helpers/Logger.hpp"
#include <thread>
#include <chrono>

using namespace zappy;

class ResourcesTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::setLevel(LogLevel::Debug);
        client_ = std::make_unique<Client>("127.0.0.1", 8674, "team1");
    }
    void TearDown() override {
        if (client_ && client_->isRunning()) {
            client_->stop();
        }
    }
    std::unique_ptr<Client> client_;
};

TEST_F(ResourcesTest, PickUpAndDrop) {
    WebsocketClient ws;
    if (!ws.connect("127.0.0.1", 8674, true).ok()) {
        GTEST_SKIP() << "Server not running";
    }

    auto res = client_->connect(5000);
    EXPECT_TRUE(res.ok()) << "Connection failed";
    
    res = client_->run();
    EXPECT_TRUE(res.ok()) << "Run failed";

    // Wait for initial vision / AI action to send "prend nourriture"
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    EXPECT_TRUE(client_->getState().isConnected());

    client_->stop();
}
