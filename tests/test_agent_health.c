#include "test.h"
#include "nevercode.h"

static int test_ah_lifecycle(void) {
    agent_health_free();
    agent_health_init();
    ASSERT_INT_EQ(agent_health_register("a1"), 0);
    ASSERT_INT_EQ(agent_health_set_phase("a1", 2), 0);
    ASSERT_INT_EQ(agent_health_record_turn("a1"), 0);
    ASSERT_INT_EQ(agent_health_record_tool("a1", "Bash"), 0);
    ASSERT_INT_EQ(agent_health_record_error("a1"), 0);
    ASSERT_INT_EQ(agent_health_set_progress("a1", "done % ok"), 0);
    ASSERT_INT_EQ(agent_health_count(), 1);
    char *one = agent_health_format("a1");
    ASSERT_NOT_NULL(one);
    ASSERT_STR_CONTAINS(one, "a1");
    ASSERT_STR_CONTAINS(one, "done % ok");
    free(one);
    char *all = agent_health_format_all();
    ASSERT_NOT_NULL(all);
    free(all);
    ASSERT(agent_health_idle_ms("a1") >= 0);
    ASSERT_INT_EQ(agent_health_unregister("a1"), 0);
    ASSERT_INT_EQ(agent_health_count(), 0);
    agent_health_free();
    return 0;
}

int test_agent_health_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_health");
    RUN_TEST(test_ah_lifecycle);
    TEST_RETURN_FAILED();
}
