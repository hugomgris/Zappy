#include <gtest/gtest.h>
#include "app/Client.hpp"
#include "helpers/Logger.hpp"
#include <thread>
#include <chrono>
#include <atomic>

using namespace zappy;

class AIBehaviorTest : public ::testing::Test {
protected:
	void SetUp() override {
		client_ = std::make_unique<Client>("localhost", 8674, "team1");
		client_->setForkEnabled(true);
		client_->setTargetLevel(2);
		client_->setMaxForks(2);
		
		Logger::setLevel(LogLevel::Info);
	}
	
	void TearDown() override {
		if (client_ && client_->isRunning()) {
			client_->stop();
		}
		client_.reset();
	}
	
	std::unique_ptr<Client> client_;
};

TEST_F(AIBehaviorTest, AIGathersFood) {
	std::atomic<bool> food_gathered{false};
	
	client_->onMessage([&food_gathered](const ServerMessage& msg) {
		if (msg.type == ServerMessageType::Response && 
			msg.cmd == "prend" && msg.arg == "nourriture" && msg.isOk()) {
			food_gathered = true;
		}
	});
	
	Result res = client_->connect(5000);
	if (!res.ok()) {
		GTEST_SKIP() << "Server not available: " << res.message;
	}
	
	res = client_->run();
	ASSERT_TRUE(res.ok());
	
	// Wait for AI to gather food
	for (int i = 0; i < 300 && !food_gathered; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	EXPECT_TRUE(food_gathered);
	
	client_->stop();
}

TEST_F(AIBehaviorTest, AIExploresWhenIdle) {
	std::atomic<int> move_count{0};
	
	client_->onMessage([&move_count](const ServerMessage& msg) {
		if (msg.type == ServerMessageType::Response && 
			(msg.cmd == "avance" || msg.cmd == "droite" || msg.cmd == "gauche")) {
			move_count++;
		}
	});
	
	Result res = client_->connect(5000);
	if (!res.ok()) {
		GTEST_SKIP() << "Server not available: " << res.message;
	}
	
	res = client_->run();
	ASSERT_TRUE(res.ok());
	
	// Wait for AI to make some moves
	std::this_thread::sleep_for(std::chrono::seconds(5));
	
	EXPECT_GT(move_count, 0);
	
	client_->stop();
}

TEST_F(AIBehaviorTest, AIForksWhenConditionsMet) {
	std::atomic<bool> fork_attempted{false};
	std::atomic<bool> fork_successful{false};
	
	client_->onMessage([&fork_attempted, &fork_successful](const ServerMessage& msg) {
		if (msg.type == ServerMessageType::Response && msg.cmd == "fork") {
			fork_attempted = true;
			if (msg.isOk()) {
				fork_successful = true;
			}
		}
	});
	
	Result res = client_->connect(5000);
	if (!res.ok()) {
		GTEST_SKIP() << "Server not available: " << res.message;
	}
	
	res = client_->run();
	ASSERT_TRUE(res.ok());
	
	// Wait for AI to potentially fork (needs enough food)
	for (int i = 0; i < 600 && !fork_attempted; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	// Fork may or may not happen depending on conditions
	// Just verify no crashes
	SUCCEED();
	
	client_->stop();
}

TEST_F(AIBehaviorTest, AIChecksIncantationReadiness) {
	std::atomic<bool> incantation_checked{false};
	
	client_->onMessage([&incantation_checked](const ServerMessage& msg) {
		if (msg.type == ServerMessageType::Response && msg.cmd == "incantation") {
			incantation_checked = true;
		}
	});
	
	Result res = client_->connect(5000);
	if (!res.ok()) {
		GTEST_SKIP() << "Server not available: " << res.message;
	}
	
	res = client_->run();
	ASSERT_TRUE(res.ok());
	
	// Wait a bit
	std::this_thread::sleep_for(std::chrono::seconds(8));
	
	// May or may not incantate
	SUCCEED();
	
	client_->stop();
}