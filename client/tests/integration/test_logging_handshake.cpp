#include <gtest/gtest.h>
#include "app/Client.hpp"
#include "helpers/Logger.hpp"
#include <thread>
#include <chrono>

using namespace zappy;

class LoggingHandshakeTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::setLevel(LogLevel::Debug);
        client_ = std::make_unique<Client>("127.0.0.1", 8674, "team1");
    }

    void TearDown() override {
        if (client_ && client_->isRunning()) {
            client_->stop();
        }
        client_.reset();
    }

    bool isServerRunning() {
        WebsocketClient ws;
        Result res = ws.connect("127.0.0.1", 8674, true);
        return res.ok();
    }

    std::unique_ptr<Client> client_;
};

TEST_F(LoggingHandshakeTest, ConnectsAndLogsHandshake) {
    if (!isServerRunning()) {
        GTEST_SKIP() << "Zappy server not running on 127.0.0.1:8674";
    }

    bool received_bienvenue = false;
    bool received_welcome = false;

    client_->onMessage([&](const ServerMessage& msg) {
        if (msg.type == ServerMessageType::Bienvenue) {
            received_bienvenue = true;
        } else if (msg.type == ServerMessageType::Welcome) {
            received_welcome = true;
        }
    });

    Result res = client_->connect(5000);
    EXPECT_TRUE(res.ok()) << "Connection failed: " << res.message;
    
    res = client_->run();
    EXPECT_TRUE(res.ok()) << "Run failed: " << res.message;

    // Wait slightly to let the handshake complete
    for(int i = 0; i < 20; i++) {
        if (received_bienvenue && received_welcome) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(received_bienvenue) << "Did not receive BIENVENUE message";
    EXPECT_TRUE(received_welcome) << "Did not receive team welcome message (mapSize, etc)";
    
    EXPECT_TRUE(client_->getState().isConnected());

    client_->stop();
}
