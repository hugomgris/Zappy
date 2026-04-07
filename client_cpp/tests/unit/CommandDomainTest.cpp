#include "gtest/gtest.h"
#include "app/command/CommandRequest.hpp"

TEST(CommandDomainTest, ComputesDeadlineFromSpec) {
	const std::int64_t	now = 10000;
	CommandRequest		req = CommandRequest::make(42, CommandType::Login, now);

	EXPECT_EQ(req.id, 42u);
	EXPECT_EQ(req.enqueuedAtMs, now);
	EXPECT_EQ(req.deadlineAtMs, now + req.spec.timeoutMs);
}

TEST(CommandDomainTest, RetryStartsAtZero) {
	CommandRequest req = CommandRequest::make(1, CommandType::Prend, 1234);

	EXPECT_EQ(req.retryCount, 0);
}

TEST(CommandDomainTest, PrendHasRetries) {
	CommandRequest req = CommandRequest::make(7, CommandType::Prend, 0);
	EXPECT_GE(req.spec.maxRetries, 1);
}