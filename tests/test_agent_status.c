#include "test.h"
#include "nevercode.h"

static int test_running(void) {
    ASSERT_STR_EQ(agent_status_name(AGENT_STATUS_RUNNING), "running");
    return 0;
}

static int test_completed(void) {
    ASSERT_STR_EQ(agent_status_name(AGENT_STATUS_COMPLETED), "completed");
    return 0;
}

static int test_failed(void) {
    ASSERT_STR_EQ(agent_status_name(AGENT_STATUS_FAILED), "failed");
    return 0;
}

static int test_paused(void) {
    ASSERT_STR_EQ(agent_status_name(AGENT_STATUS_PAUSED), "paused");
    return 0;
}

static int test_unknown(void) {
    ASSERT_STR_EQ(agent_status_name((agent_status_t)999), "unknown");
    return 0;
}

static int test_negative(void) {
    const char *n = agent_status_name((agent_status_t)-1);
    ASSERT_NOT_NULL(n);
    ASSERT_STR_EQ(n, "unknown");
    return 0;
}

int test_agent_status_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_status");
    RUN_TEST(test_running);
    RUN_TEST(test_completed);
    RUN_TEST(test_failed);
    RUN_TEST(test_paused);
    RUN_TEST(test_unknown);
    RUN_TEST(test_negative);
    TEST_RETURN_FAILED();
}
