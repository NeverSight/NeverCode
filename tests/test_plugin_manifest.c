#include "test.h"
#include "nevercode.h"
static int t1(void) {
    plugin_manifest_init();
    ASSERT_INT_EQ(0, plugin_manifest_count());
    plugin_manifest_free();
    return 0;
}
static int t2(void) {
    plugin_manifest_init();
    int n = plugin_manifest_discover("/nonexistent");
    ASSERT_INT_EQ(0, n);
    plugin_manifest_free();
    return 0;
}
static int t3(void) {
    plugin_manifest_init();
    ASSERT_INT_EQ(-1, plugin_manifest_find("nope"));
    plugin_manifest_free();
    return 0;
}
static int t4(void) {
    plugin_manifest_init();
    char *s = plugin_manifest_format_list();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Manifest");
    free(s);
    plugin_manifest_free();
    return 0;
}
int test_plugin_manifest_all(void) {
    TEST_SUITE("plugin_manifest");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    TEST_RETURN_FAILED();
}
