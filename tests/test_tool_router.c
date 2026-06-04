#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include <stdlib.h>
#include <string.h>
static int test_router_init_free(void) {
    tool_router_init();
    ASSERT_INT_EQ(tool_router_total(), 0);
    tool_router_free();
    return 0;
}
static int test_router_classify_shell(void) {
    tool_router_init();
    routed_call_t rc;
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "call_id", "c1");
    ASSERT_INT_EQ(tool_router_classify("bash", item, &rc), 0);
    ASSERT_INT_EQ(rc.kind, PAYLOAD_LOCAL_SHELL);
    ASSERT_STR_EQ(rc.call_id, "c1");
    routed_call_free(&rc);
    cJSON_Delete(item);
    tool_router_free();
    return 0;
}
static int test_router_classify_mcp(void) {
    tool_router_init();
    tool_router_add_mcp_prefix("mcp__");
    routed_call_t rc;
    ASSERT_INT_EQ(tool_router_classify("mcp__s__t", NULL, &rc), 0);
    ASSERT_INT_EQ(rc.kind, PAYLOAD_MCP);
    routed_call_free(&rc);
    tool_router_free();
    return 0;
}
static int test_router_classify_search(void) {
    tool_router_init();
    routed_call_t rc;
    ASSERT_INT_EQ(tool_router_classify("tool_search", NULL, &rc), 0);
    ASSERT_INT_EQ(rc.kind, PAYLOAD_SEARCH);
    routed_call_free(&rc);
    tool_router_free();
    return 0;
}
static int test_router_stats(void) {
    tool_router_init();
    routed_call_t rc;
    tool_router_classify("bash", NULL, &rc);
    routed_call_free(&rc);
    tool_router_classify("tool_search", NULL, &rc);
    routed_call_free(&rc);
    ASSERT_INT_EQ(tool_router_total(), 2);
    char *s = tool_router_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "total=2");
    free(s);
    tool_router_free();
    return 0;
}
static int test_router_names(void) {
    ASSERT_STR_EQ(payload_kind_name(PAYLOAD_FUNCTION), "function");
    ASSERT_STR_EQ(payload_kind_name(PAYLOAD_LOCAL_SHELL), "shell");
    return 0;
}
int test_tool_router_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_router");
    RUN_TEST(test_router_init_free);
    RUN_TEST(test_router_classify_shell);
    RUN_TEST(test_router_classify_mcp);
    RUN_TEST(test_router_classify_search);
    RUN_TEST(test_router_stats);
    RUN_TEST(test_router_names);
    TEST_RETURN_FAILED();
}
