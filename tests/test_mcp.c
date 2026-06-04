#include "test.h"
#include "mcp.h"
#include <string.h>

static int test_server_init(void) {
    mcp_server_t s;
    memset(&s, 0, sizeof(s));
    ASSERT_NULL(s.name);
    ASSERT_INT_EQ(s.pid, 0);
    ASSERT_INT_EQ(s.num_tools, 0);
    return 0;
}

static int test_stop_noop(void) {
    mcp_server_t s;
    memset(&s, 0, sizeof(s));
    mcp_stop(&s);
    ASSERT_INT_EQ(s.pid, 0);
    return 0;
}

static int test_discover_noop(void) {
    mcp_server_t s;
    memset(&s, 0, sizeof(s));
    int n = mcp_discover_tools(&s);
    ASSERT_TRUE(n <= 0);
    return 0;
}

static int test_tool_array_size(void) {
    mcp_server_t s;
    memset(&s, 0, sizeof(s));
    ASSERT_INT_EQ(s.num_tools, 0);
    ASSERT_TRUE(sizeof(s.tools) > 0);
    return 0;
}

int test_mcp_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("mcp");
    RUN_TEST(test_server_init);
    RUN_TEST(test_stop_noop);
    RUN_TEST(test_discover_noop);
    RUN_TEST(test_tool_array_size);
    TEST_RETURN_FAILED();
}
