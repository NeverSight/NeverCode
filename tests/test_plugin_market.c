#include "test.h"
#include "nevercode.h"

int plugin_market_scan(const char *project_root);
int plugin_market_count(void);
int plugin_market_marketplace_count(void);
const char *plugin_market_plugin_name(int market_idx, int plugin_idx);
char *plugin_market_format_list(void);

static int test_scan_empty(void) {
    int r = plugin_market_scan("/tmp/nevercode_test_no_plugins");
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(plugin_market_count(), 0);
    ASSERT_INT_EQ(plugin_market_marketplace_count(), 0);
    return 0;
}

static int test_format_empty(void) {
    plugin_market_scan("/tmp/nonexistent");
    char *s = plugin_market_format_list();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "No plugin");
    free(s);
    return 0;
}

static int test_invalid_indices(void) {
    ASSERT_NULL(plugin_market_plugin_name(-1, 0));
    ASSERT_NULL(plugin_market_plugin_name(0, -1));
    ASSERT_NULL(plugin_market_plugin_name(999, 0));
    return 0;
}

int test_plugin_market_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("plugin_market");
    RUN_TEST(test_scan_empty);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_invalid_indices);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("plugin_market");
    RUN_TEST(test_scan_empty);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_invalid_indices);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
