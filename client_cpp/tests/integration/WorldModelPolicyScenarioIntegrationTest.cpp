#include <gtest/gtest.h>

#include "app/policy/WorldModelPolicy.hpp"

#include <optional>
#include <string>

namespace {
	std::string makeInventoryPayload(int food, int linemate, int deraumere, int sibur) {
		return std::string("{\"type\":\"response\",\"cmd\":\"inventaire\",\"arg\":{")
			+ "\"nourriture\":" + std::to_string(food)
			+ ",\"linemate\":" + std::to_string(linemate)
			+ ",\"deraumere\":" + std::to_string(deraumere)
			+ ",\"sibur\":" + std::to_string(sibur)
			+ "}}";
	}

	CommandType intentToCommandType(const std::shared_ptr<IntentRequest>& intent) {
		if (dynamic_cast<const RequestVoir*>(intent.get()) != nullptr) {
			return CommandType::Voir;
		}
		if (dynamic_cast<const RequestInventaire*>(intent.get()) != nullptr) {
			return CommandType::Inventaire;
		}
		if (dynamic_cast<const RequestMove*>(intent.get()) != nullptr) {
			return CommandType::Avance;
		}
		if (dynamic_cast<const RequestTurnLeft*>(intent.get()) != nullptr) {
			return CommandType::Gauche;
		}
		if (dynamic_cast<const RequestTurnRight*>(intent.get()) != nullptr) {
			return CommandType::Droite;
		}
		if (dynamic_cast<const RequestBroadcast*>(intent.get()) != nullptr) {
			return CommandType::Broadcast;
		}
		if (dynamic_cast<const RequestIncantation*>(intent.get()) != nullptr) {
			return CommandType::Incantation;
		}
		if (dynamic_cast<const RequestTake*>(intent.get()) != nullptr) {
			return CommandType::Prend;
		}

		return CommandType::Unknown;
	}
}

TEST(WorldModelPolicyScenarioIntegrationTest, StarvationPreventionUnderSparseFood) {
	WorldModelPolicy policy(5000, 7000, 8000, 5000, 10, 1, 4000, 999999);

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = makeInventoryPayload(3, 0, 0, 0);
	EXPECT_TRUE(policy.onCommandEvent(10, inventoryEvent, std::nullopt).empty());

	CommandEvent noFoodVision;
	noFoodVision.commandType = CommandType::Voir;
	noFoodVision.status = CommandStatus::Success;
	noFoodVision.details = R"([[],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(20, noFoodVision, std::nullopt).empty());

	auto sparseIntents = policy.onTick(30);
	ASSERT_EQ(sparseIntents.size(), 2UL);
	EXPECT_EQ(sparseIntents[0]->description(), "RequestTurnLeft");
	EXPECT_EQ(sparseIntents[1]->description(), "RequestMove");

	CommandEvent foodVision;
	foodVision.commandType = CommandType::Voir;
	foodVision.status = CommandStatus::Success;
	foodVision.details = R"([["nourriture"],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(40, foodVision, std::nullopt).empty());

	auto foodIntents = policy.onTick(50);
	ASSERT_EQ(foodIntents.size(), 1UL);
	EXPECT_EQ(foodIntents[0]->description(), "RequestTake(nourriture)");
}

TEST(WorldModelPolicyScenarioIntegrationTest, GathersNeededResourceThenTriggersIncantationReadiness) {
	WorldModelPolicy policy(5000, 7000, 8000, 5000, 10, 1, 4000, 999999);

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = makeInventoryPayload(12, 0, 1, 1);
	EXPECT_TRUE(policy.onCommandEvent(10, inventoryEvent, std::nullopt).empty());

	CommandEvent gatherVision;
	gatherVision.commandType = CommandType::Voir;
	gatherVision.status = CommandStatus::Success;
	gatherVision.details = R"([["linemate"],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(20, gatherVision, std::nullopt).empty());

	auto gatherIntents = policy.onTick(30);
	ASSERT_EQ(gatherIntents.size(), 1UL);
	EXPECT_EQ(gatherIntents[0]->description(), "RequestTake(linemate)");

	CommandEvent takeEvent;
	takeEvent.commandType = CommandType::Prend;
	takeEvent.status = CommandStatus::Success;
	takeEvent.details = "ok";
	EXPECT_TRUE(policy.onCommandEvent(40, takeEvent, std::nullopt).empty());

	CommandEvent readyInventory;
	readyInventory.commandType = CommandType::Inventaire;
	readyInventory.status = CommandStatus::Success;
	readyInventory.details = makeInventoryPayload(12, 1, 1, 1);
	EXPECT_TRUE(policy.onCommandEvent(50, readyInventory, std::nullopt).empty());

	CommandEvent readyVision;
	readyVision.commandType = CommandType::Voir;
	readyVision.status = CommandStatus::Success;
	readyVision.details = R"([["player"],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(60, readyVision, std::nullopt).empty());

	auto readyIntents = policy.onTick(70);
	ASSERT_EQ(readyIntents.size(), 1UL);
	EXPECT_EQ(readyIntents[0]->description(), "RequestIncantation");
}

TEST(WorldModelPolicyScenarioIntegrationTest, ReroutesWhenFreshVisionContradictsPreviousPlan) {
	WorldModelPolicy policy(5000, 7000, 8000, 5000, 10, 1, 4000, 999999);

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = makeInventoryPayload(9, 0, 0, 0);
	EXPECT_TRUE(policy.onCommandEvent(10, inventoryEvent, std::nullopt).empty());

	CommandEvent initialVision;
	initialVision.commandType = CommandType::Voir;
	initialVision.status = CommandStatus::Success;
	initialVision.details = R"([[],[],[],["nourriture"]])";
	EXPECT_TRUE(policy.onCommandEvent(20, initialVision, std::nullopt).empty());

	auto initialPlan = policy.onTick(30);
	ASSERT_EQ(initialPlan.size(), 4UL);
	EXPECT_EQ(initialPlan[0]->description(), "RequestTurnRight");

	CommandEvent correctedVision;
	correctedVision.commandType = CommandType::Voir;
	correctedVision.status = CommandStatus::Success;
	correctedVision.details = R"([[],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(31, correctedVision, std::nullopt).empty());

	auto replanned = policy.onTick(32);
	ASSERT_EQ(replanned.size(), 2UL);
	EXPECT_EQ(replanned[0]->description(), "RequestTurnLeft");
	EXPECT_EQ(replanned[1]->description(), "RequestMove");
}

TEST(WorldModelPolicyScenarioIntegrationTest, LongRunSimulationMaintainsFoodPriorityAndAvoidsDeadloop) {
	WorldModelPolicy policy(5000, 7000, 8000, 5000, 10, 1, 4000, 999999);

	int food = 12;
	int linemate = 0;
	int deraumere = 0;
	int sibur = 0;
	int progressActions = 0;

	for (int tick = 0; tick < 1200; ++tick) {
		const std::int64_t now = static_cast<std::int64_t>(tick) * 50 + 100;

		if (tick % 5 == 0 && food > 0) {
			--food;
		}

		const bool lowFood = food < 6;
		const bool currentTileFood = (tick % 3 == 0);
		const bool currentTileLinemate = (linemate == 0 && tick % 11 == 0);

		CommandEvent inventoryEvent;
		inventoryEvent.commandType = CommandType::Inventaire;
		inventoryEvent.status = CommandStatus::Success;
		inventoryEvent.details = makeInventoryPayload(food, linemate, deraumere, sibur);
		EXPECT_TRUE(policy.onCommandEvent(now - 2, inventoryEvent, std::nullopt).empty());

		std::string visionPayload = "[[";
		bool firstToken = true;
		if (currentTileFood) {
			visionPayload += "\"nourriture\"";
			firstToken = false;
		}
		if (currentTileLinemate) {
			if (!firstToken) {
				visionPayload += ",";
			}
			visionPayload += "\"linemate\"";
		}
		visionPayload += "],[],[]]";

		CommandEvent visionEvent;
		visionEvent.commandType = CommandType::Voir;
		visionEvent.status = CommandStatus::Success;
		visionEvent.details = visionPayload;
		EXPECT_TRUE(policy.onCommandEvent(now - 1, visionEvent, std::nullopt).empty());

		auto intents = policy.onTick(now);
		ASSERT_FALSE(intents.empty());

		if (lowFood && currentTileFood) {
			EXPECT_EQ(intents.front()->description(), "RequestTake(nourriture)");
		}

		for (const auto& intent : intents) {
			const CommandType type = intentToCommandType(intent);
			ASSERT_NE(type, CommandType::Unknown);

			if (const auto* take = dynamic_cast<const RequestTake*>(intent.get())) {
				if (take->resource == ResourceType::Nourriture && currentTileFood) {
					food += 3;
					++progressActions;
				}
				if (take->resource == ResourceType::Linemate && currentTileLinemate) {
					++linemate;
					++progressActions;
				}
			}

			if (type == CommandType::Avance || type == CommandType::Incantation) {
				++progressActions;
			}

			CommandEvent completion;
			completion.commandType = type;
			completion.status = CommandStatus::Success;
			completion.details = "ok";
			EXPECT_TRUE(policy.onCommandEvent(now + 1, completion, std::nullopt).empty());
		}
	}

	EXPECT_GT(progressActions, 50);
}
