#include <gtest/gtest.h>

#include "app/policy/WorldModelPolicy.hpp"

TEST(WorldModelPolicyTest, EmitsSensorRefreshIntentsWhenStateIsMissingOrStale) {
	WorldModelPolicy policy(5000, 7000);

	auto intents = policy.onTick(0);
	ASSERT_EQ(intents.size(), 2UL);
	EXPECT_EQ(intents[0]->description(), "RequestVoir");
	EXPECT_EQ(intents[1]->description(), "RequestInventaire");

	CommandEvent visionEvent;
	visionEvent.commandType = CommandType::Voir;
	visionEvent.status = CommandStatus::Success;
	visionEvent.details = R"([["nourriture"],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(10, visionEvent, std::nullopt).empty());

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = R"({"type":"response","cmd":"inventaire","arg":{"nourriture":9}})";
	EXPECT_TRUE(policy.onCommandEvent(20, inventoryEvent, std::nullopt).empty());

	auto freshIntents = policy.onTick(1000);
	ASSERT_EQ(freshIntents.size(), 1UL);
	EXPECT_EQ(freshIntents[0]->description(), "RequestTake(nourriture)");

	CommandEvent takeEvent;
	takeEvent.commandType = CommandType::Prend;
	takeEvent.status = CommandStatus::Success;
	takeEvent.details = "ok";
	EXPECT_TRUE(policy.onCommandEvent(1010, takeEvent, std::nullopt).empty());

	CommandEvent turnEvent;
	turnEvent.commandType = CommandType::Gauche;
	turnEvent.status = CommandStatus::Success;
	turnEvent.details = "ok";
	EXPECT_TRUE(policy.onCommandEvent(1020, turnEvent, std::nullopt).empty());

	CommandEvent moveEvent;
	moveEvent.commandType = CommandType::Avance;
	moveEvent.status = CommandStatus::Success;
	moveEvent.details = "ok";
	EXPECT_TRUE(policy.onCommandEvent(1030, moveEvent, std::nullopt).empty());

	auto visionStaleIntents = policy.onTick(6010);
	ASSERT_EQ(visionStaleIntents.size(), 1UL);
	EXPECT_EQ(visionStaleIntents[0]->description(), "RequestVoir");
	ASSERT_TRUE(policy.state().pose().has_value());
	EXPECT_EQ(policy.state().pose()->orientation, 4);
	EXPECT_EQ(policy.state().pose()->x, -1);
	EXPECT_EQ(policy.state().pose()->y, 0);
}

TEST(WorldModelPolicyTest, UpdatesWorldStateFromSuccessfulCommandEvents) {
	WorldModelPolicy policy(5000, 7000);

	CommandEvent visionEvent;
	visionEvent.commandType = CommandType::Voir;
	visionEvent.status = CommandStatus::Success;
	visionEvent.details = "[{\"tile\":0}]";
	EXPECT_TRUE(policy.onCommandEvent(100, visionEvent, std::nullopt).empty());
	EXPECT_TRUE(policy.state().hasVision());
	EXPECT_EQ(policy.state().lastVisionPayload(), "[{\"tile\":0}]");

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = R"({"type":"response","cmd":"inventaire","arg":{"nourriture":9,"linemate":2}})";
	EXPECT_TRUE(policy.onCommandEvent(200, inventoryEvent, std::nullopt).empty());
	EXPECT_TRUE(policy.state().hasInventory());
	EXPECT_EQ(policy.state().inventoryCount(ResourceType::Nourriture), 9);
	EXPECT_EQ(policy.state().inventoryCount(ResourceType::Linemate), 2);

	CommandEvent turnEvent;
	turnEvent.commandType = CommandType::Droite;
	turnEvent.status = CommandStatus::Success;
	turnEvent.details = "ok";
	EXPECT_TRUE(policy.onCommandEvent(300, turnEvent, std::nullopt).empty());
	ASSERT_TRUE(policy.state().pose().has_value());
	EXPECT_EQ(policy.state().pose()->orientation, 2);

	CommandEvent moveEvent;
	moveEvent.commandType = CommandType::Avance;
	moveEvent.status = CommandStatus::Success;
	moveEvent.details = "ok";
	EXPECT_TRUE(policy.onCommandEvent(400, moveEvent, std::nullopt).empty());
	EXPECT_EQ(policy.state().pose()->x, 1);
	EXPECT_EQ(policy.state().pose()->y, 0);
}

TEST(WorldModelPolicyTest, BroadcastCompletionStoresSemanticMemory) {
	WorldModelPolicy policy(5000, 7000);

	CommandEvent broadcastEvent;
	broadcastEvent.commandType = CommandType::Broadcast;
	broadcastEvent.status = CommandStatus::Success;
	broadcastEvent.details = "ok";

	IntentResult intentResult(42, "RequestBroadcast(\"help\")", true, "ok");
	EXPECT_TRUE(policy.onCommandEvent(300, broadcastEvent, intentResult).empty());
	EXPECT_TRUE(policy.state().hasBroadcast());
	EXPECT_EQ(policy.state().lastBroadcastPayload(), "RequestBroadcast(\"help\")");
}

TEST(WorldModelPolicyTest, ResourceStrategyTargetsStoneDeficitsWhenFoodIsComfortable) {
	WorldModelPolicy policy(5000, 7000);

	CommandEvent visionEvent;
	visionEvent.commandType = CommandType::Voir;
	visionEvent.status = CommandStatus::Success;
	visionEvent.details = R"([["nourriture","linemate"],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(10, visionEvent, std::nullopt).empty());

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = R"({"type":"response","cmd":"inventaire","arg":{"nourriture":20,"linemate":0,"deraumere":0,"sibur":0}})";
	EXPECT_TRUE(policy.onCommandEvent(20, inventoryEvent, std::nullopt).empty());

	const auto intents = policy.onTick(100);
	ASSERT_EQ(intents.size(), 1UL);
	EXPECT_EQ(intents[0]->description(), "RequestTake(linemate)");
}

TEST(WorldModelPolicyTest, EmitsIncantationWhenReadinessIsMet) {
	WorldModelPolicy policy(5000, 7000, 8000, 5000, 10, 1);

	CommandEvent visionEvent;
	visionEvent.commandType = CommandType::Voir;
	visionEvent.status = CommandStatus::Success;
	visionEvent.details = R"([["player"],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(10, visionEvent, std::nullopt).empty());

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = R"({"type":"response","cmd":"inventaire","arg":{"nourriture":15,"linemate":1,"deraumere":1,"sibur":1}})";
	EXPECT_TRUE(policy.onCommandEvent(20, inventoryEvent, std::nullopt).empty());

	const auto intents = policy.onTick(100);
	ASSERT_EQ(intents.size(), 1UL);
	EXPECT_EQ(intents[0]->description(), "RequestIncantation");
}

TEST(WorldModelPolicyTest, EmitsSummonBroadcastWhenResourcesReadyButPlayersMissing) {
	WorldModelPolicy policy(5000, 7000, 8000, 5000, 10, 1);

	CommandEvent visionEvent;
	visionEvent.commandType = CommandType::Voir;
	visionEvent.status = CommandStatus::Success;
	visionEvent.details = R"([[],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(10, visionEvent, std::nullopt).empty());

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = R"({"type":"response","cmd":"inventaire","arg":{"nourriture":15,"linemate":1,"deraumere":1,"sibur":1}})";
	EXPECT_TRUE(policy.onCommandEvent(20, inventoryEvent, std::nullopt).empty());

	const auto intents = policy.onTick(100);
	ASSERT_EQ(intents.size(), 1UL);
	EXPECT_EQ(intents[0]->description(), "RequestBroadcast(\"need_players_for_incantation\")");
}

TEST(WorldModelPolicyTest, ReactsToIncomingNeedPlayersTeamMessage) {
	WorldModelPolicy policy(5000, 7000, 8000, 5000, 10, 1);

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = R"({"type":"response","cmd":"inventaire","arg":{"nourriture":14,"linemate":0}})";
	EXPECT_TRUE(policy.onCommandEvent(20, inventoryEvent, std::nullopt).empty());

	CommandEvent incomingMessage;
	incomingMessage.commandType = CommandType::Broadcast;
	incomingMessage.status = CommandStatus::Success;
	incomingMessage.details = "team:need:players";

	const auto followUps = policy.onCommandEvent(100, incomingMessage, std::nullopt);
	ASSERT_EQ(followUps.size(), 1UL);
	EXPECT_EQ(followUps[0]->description(), "RequestBroadcast(\"team:on_my_way\")");
}

TEST(WorldModelPolicyTest, IgnoresIncomingNeedPlayersWhenFoodIsUnsafe) {
	WorldModelPolicy policy(5000, 7000, 8000, 5000, 10, 1);

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = R"({"type":"response","cmd":"inventaire","arg":{"nourriture":4}})";
	EXPECT_TRUE(policy.onCommandEvent(20, inventoryEvent, std::nullopt).empty());

	CommandEvent incomingMessage;
	incomingMessage.commandType = CommandType::Broadcast;
	incomingMessage.status = CommandStatus::Success;
	incomingMessage.details = "team:need:players";

	EXPECT_TRUE(policy.onCommandEvent(100, incomingMessage, std::nullopt).empty());
}

TEST(WorldModelPolicyTest, BroadcastsRoleIntentOnCadenceWhenStateIsFresh) {
	WorldModelPolicy policy(5000, 7000, 8000, 5000, 10, 1, 4000, 100);

	CommandEvent visionEvent;
	visionEvent.commandType = CommandType::Voir;
	visionEvent.status = CommandStatus::Success;
	visionEvent.details = R"([[],[],[]])";
	EXPECT_TRUE(policy.onCommandEvent(10, visionEvent, std::nullopt).empty());

	CommandEvent inventoryEvent;
	inventoryEvent.commandType = CommandType::Inventaire;
	inventoryEvent.status = CommandStatus::Success;
	inventoryEvent.details = R"({"type":"response","cmd":"inventaire","arg":{"nourriture":8}})";
	EXPECT_TRUE(policy.onCommandEvent(20, inventoryEvent, std::nullopt).empty());

	const auto intents = policy.onTick(200);
	ASSERT_EQ(intents.size(), 1UL);
	EXPECT_EQ(intents[0]->description(), "RequestBroadcast(\"team:role:gatherer\")");
}
