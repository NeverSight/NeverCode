#include "test.h"
#include "nevercode.h"

static int test_init_free(void) {
    lsp_bridge_init();
    ASSERT_INT_EQ(lsp_bridge_count(), 0);
    lsp_bridge_free();
    return 0;
}

static int test_state_names(void) {
    ASSERT_STR_EQ(lsp_state_name(LSP_DISCONNECTED), "disconnected");
    ASSERT_STR_EQ(lsp_state_name(LSP_STARTING), "starting");
    ASSERT_STR_EQ(lsp_state_name(LSP_READY), "ready");
    ASSERT_STR_EQ(lsp_state_name(LSP_SHUTTING_DOWN), "shutting_down");
    ASSERT_STR_EQ(lsp_state_name(LSP_ERROR), "error");
    return 0;
}

static int test_format_empty(void) {
    lsp_bridge_init();
    char *fmt = lsp_bridge_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_TRUE(strstr(fmt, "0 connection") != NULL);
    free(fmt);
    lsp_bridge_free();
    return 0;
}

static int test_execute_no_server(void) {
    lsp_bridge_init();
    char *r = lsp_bridge_execute("c", "definition", "/tmp/x.c", 1, 1, NULL);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(strstr(r, "No LSP server") != NULL);
    free(r);
    lsp_bridge_free();
    return 0;
}

static int test_execute_bad_op(void) {
    lsp_bridge_init();
    char *r = lsp_bridge_execute("c", "nonexistent", "/tmp/x.c", 1, 1, NULL);
    ASSERT_NOT_NULL(r);
    free(r);
    lsp_bridge_free();
    return 0;
}

static int test_stop_invalid(void) {
    lsp_bridge_init();
    ASSERT_INT_EQ(lsp_bridge_stop(-1), -1);
    ASSERT_INT_EQ(lsp_bridge_stop(99), -1);
    lsp_bridge_free();
    return 0;
}

static int test_did_open_no_server(void) {
    lsp_bridge_init();
    ASSERT_INT_EQ(lsp_bridge_did_open("c", "/tmp/x.c", "int main(){}", "c"), -1);
    lsp_bridge_free();
    return 0;
}

int test_lsp_bridge_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("lsp_bridge");
    RUN_TEST(test_init_free);
    RUN_TEST(test_state_names);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_execute_no_server);
    RUN_TEST(test_execute_bad_op);
    RUN_TEST(test_stop_invalid);
    RUN_TEST(test_did_open_no_server);
    TEST_RETURN_FAILED();
}
