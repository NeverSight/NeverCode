#include "test.h"
#include "repl.h"

static int test_perms_addr(void) {
    ASSERT_NOT_NULL(&g_perms);
    return 0;
}

static int test_hooks_addr(void) {
    ASSERT_NOT_NULL(&g_hooks);
    return 0;
}

static int test_memory_addr(void) {
    ASSERT_NOT_NULL(&g_memory);
    return 0;
}

static int test_mcp_servers_addr(void) {
    ASSERT_NOT_NULL(g_mcp_servers);
    ASSERT_GE(MAX_MCP_SERVERS, 1);
    return 0;
}

static int test_initial_values(void) {
    ASSERT_INT_EQ(g_total_input_tokens, 0);
    ASSERT_INT_EQ(g_total_output_tokens, 0);
    ASSERT_FALSE(g_plan_mode);
    ASSERT_FALSE(g_brief_mode);
    return 0;
}

int test_repl_globals_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("repl_globals");
    RUN_TEST(test_perms_addr);
    RUN_TEST(test_hooks_addr);
    RUN_TEST(test_memory_addr);
    RUN_TEST(test_mcp_servers_addr);
    RUN_TEST(test_initial_values);
    TEST_RETURN_FAILED();
}
