#include <gtest/gtest.h>
#include <signal.h>

class GlobalEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        signal(SIGPIPE, SIG_IGN);
    }
};

::testing::Environment* const foo_env = ::testing::AddGlobalTestEnvironment(new GlobalEnvironment);
