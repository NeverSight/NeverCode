#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_init(void) {
    plugin_inject_init();
    ASSERT_INT_EQ(plugin_inject_count(), 0);
    plugin_inject_free();
    return 0;
}
static int test_register(void) {
    plugin_inject_init();
    int dummy = 42;
    ASSERT_INT_EQ(plugin_inject_register("p1", INJECT_TOOL, "tool1", "desc", &dummy), 0);
    ASSERT_INT_EQ(plugin_inject_count(), 1);
    plugin_inject_free();
    return 0;
}
static int test_find(void) {
    plugin_inject_init();
    int dummy = 99;
    plugin_inject_register("p1", INJECT_TOOL, "tool1", "desc", &dummy);
    void *h = plugin_inject_find("tool1", INJECT_TOOL);
    ASSERT_NOT_NULL(h);
    ASSERT(*(int *)h == 99);
    ASSERT(plugin_inject_find("nonexist", INJECT_TOOL) == NULL);
    plugin_inject_free();
    return 0;
}
static int test_remove(void) {
    plugin_inject_init();
    plugin_inject_register("p1", INJECT_TOOL, "t1", NULL, NULL);
    plugin_inject_register("p1", INJECT_HOOK, "h1", NULL, NULL);
    ASSERT_INT_EQ(plugin_inject_count(), 2);
    ASSERT_INT_EQ(plugin_inject_remove("p1", "t1", INJECT_TOOL), 0);
    ASSERT_INT_EQ(plugin_inject_count(), 1);
    plugin_inject_free();
    return 0;
}
static int test_remove_all(void) {
    plugin_inject_init();
    plugin_inject_register("p1", INJECT_TOOL, "t1", NULL, NULL);
    plugin_inject_register("p1", INJECT_HOOK, "h1", NULL, NULL);
    plugin_inject_register("p2", INJECT_TOOL, "t2", NULL, NULL);
    ASSERT_INT_EQ(plugin_inject_remove_all("p1"), 2);
    ASSERT_INT_EQ(plugin_inject_count(), 1);
    plugin_inject_free();
    return 0;
}
static int test_no_dup(void) {
    plugin_inject_init();
    plugin_inject_register("p1", INJECT_TOOL, "t1", NULL, NULL);
    ASSERT_INT_EQ(plugin_inject_register("p1", INJECT_TOOL, "t1", NULL, NULL), -1);
    ASSERT_INT_EQ(plugin_inject_count(), 1);
    plugin_inject_free();
    return 0;
}
static int test_count_by_kind(void) {
    plugin_inject_init();
    plugin_inject_register("p1", INJECT_TOOL, "t1", NULL, NULL);
    plugin_inject_register("p1", INJECT_HOOK, "h1", NULL, NULL);
    plugin_inject_register("p2", INJECT_TOOL, "t2", NULL, NULL);
    ASSERT_INT_EQ(plugin_inject_count_by_kind(INJECT_TOOL), 2);
    ASSERT_INT_EQ(plugin_inject_count_by_kind(INJECT_HOOK), 1);
    plugin_inject_free();
    return 0;
}
static int test_format(void) {
    plugin_inject_init();
    plugin_inject_register("p1", INJECT_TOOL, "t1", "test tool", NULL);
    char *s = plugin_inject_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "p1");
    free(s);
    plugin_inject_free();
    return 0;
}

int test_plugin_inject_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("plugin_inject");
    RUN_TEST(test_init);
    RUN_TEST(test_register);
    RUN_TEST(test_find);
    RUN_TEST(test_remove);
    RUN_TEST(test_remove_all);
    RUN_TEST(test_no_dup);
    RUN_TEST(test_count_by_kind);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
