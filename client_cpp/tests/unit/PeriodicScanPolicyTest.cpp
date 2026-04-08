#include <gtest/gtest.h>

#include "app/policy/PeriodicScanPolicy.hpp"

TEST(PeriodicScanPolicyTest, EmitsVoirAndInventaireOnConfiguredCadence) {
	PeriodicScanPolicy policy(5000, 2000, 7000);

	auto t0 = policy.onTick(1000);
	EXPECT_TRUE(t0.empty());

	auto t1 = policy.onTick(3000);
	ASSERT_EQ(t1.size(), 1UL);
	EXPECT_EQ(t1[0]->description(), "RequestInventaire");

	auto t2 = policy.onTick(6000);
	ASSERT_EQ(t2.size(), 1UL);
	EXPECT_EQ(t2[0]->description(), "RequestVoir");

	auto t3 = policy.onTick(10000);
	ASSERT_EQ(t3.size(), 1UL);
	EXPECT_EQ(t3[0]->description(), "RequestInventaire");
}

TEST(PeriodicScanPolicyTest, EmitsBothWhenBothDeadlinesReached) {
	PeriodicScanPolicy policy(5000, 2000, 7000);

	(void)policy.onTick(0);
	auto intents = policy.onTick(15000);

	ASSERT_EQ(intents.size(), 2UL);
	EXPECT_EQ(intents[0]->description(), "RequestVoir");
	EXPECT_EQ(intents[1]->description(), "RequestInventaire");
}
