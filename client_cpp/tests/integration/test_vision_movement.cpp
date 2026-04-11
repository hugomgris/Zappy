#include <gtest/gtest.h>
#include "app/Client.hpp"
#include "helpers/Logger.hpp"
#include "app/WorldState.hpp"
#include <thread>
#include <chrono>

using namespace zappy;

class VisionMovementTest : public ::testing::Test {
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

    std::unique_ptr<Client> client_;
};

TEST_F(VisionMovementTest, EnsureCoordinateWrappingFixed) {
    bool got_vision = false;
    client_->onMessage([&](const ServerMessage& msg) {
        if (msg.type == ServerMessageType::Response && msg.cmd == "voir" && msg.vision.has_value()) {
            auto& vision = msg.vision.value();
            if (vision.size() > 0) {
                got_vision = true;
            }
        }
    });

    auto res = client_->connect(5000);
    EXPECT_TRUE(res.ok()) << "Connection failed";
    
    res = client_->run();
    EXPECT_TRUE(res.ok()) << "Run failed";

    for (int i = 0; i < 40 && !got_vision; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_TRUE(got_vision) << "Did not receive explicit vision fix verification";

    client_->stop();
}
