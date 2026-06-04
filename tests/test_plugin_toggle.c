#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

void plugin_toggle_init(void);
void plugin_toggle_free(void);
bool plugin_toggle_is_enabled(const char *id);
int plugin_toggle_set(const char *id, bool enabled);
int plugin_toggle_enable(const char *id);
int plugin_toggle_disable(const char *id);
int plugin_toggle_count(void);
int plugin_toggle_enabled_count(void);
int plugin_toggle_disabled_count(void);
char *plugin_toggle_serialize(void);
int plugin_toggle_deserialize(const char *json);
char *plugin_toggle_format(void);

static int test_init(void) {
    plugin_toggle_init();
    ASSERT_INT_EQ(plugin_toggle_count(), 0);
    plugin_toggle_free();
    return 0;
}
static int test_default_enabled(void) {
    plugin_toggle_init();
    ASSERT_TRUE(plugin_toggle_is_enabled("unknown_plugin"));
    plugin_toggle_free();
    return 0;
}
static int test_disable(void) {
    plugin_toggle_init();
    plugin_toggle_disable("p1");
    ASSERT_FALSE(plugin_toggle_is_enabled("p1"));
    ASSERT_INT_EQ(plugin_toggle_count(), 1);
    ASSERT_INT_EQ(plugin_toggle_disabled_count(), 1);
    plugin_toggle_free();
    return 0;
}
static int test_enable(void) {
    plugin_toggle_init();
    plugin_toggle_disable("p1");
    plugin_toggle_enable("p1");
    ASSERT_TRUE(plugin_toggle_is_enabled("p1"));
    ASSERT_INT_EQ(plugin_toggle_enabled_count(), 1);
    plugin_toggle_free();
    return 0;
}
static int test_serialize(void) {
    plugin_toggle_init();
    plugin_toggle_disable("p1");
    plugin_toggle_enable("p2");
    char *json = plugin_toggle_serialize();
    ASSERT_NOT_NULL(json);
    plugin_toggle_free();
    plugin_toggle_init();
    ASSERT_INT_EQ(plugin_toggle_deserialize(json), 2);
    ASSERT_FALSE(plugin_toggle_is_enabled("p1"));
    ASSERT_TRUE(plugin_toggle_is_enabled("p2"));
    free(json);
    plugin_toggle_free();
    return 0;
}
static int test_format(void) {
    plugin_toggle_init();
    plugin_toggle_disable("test_plug");
    char *s = plugin_toggle_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "test_plug");
    ASSERT_STR_CONTAINS(s, "disabled");
    free(s);
    plugin_toggle_free();
    return 0;
}

int test_plugin_toggle_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("plugin_toggle");
    RUN_TEST(test_init);
    RUN_TEST(test_default_enabled);
    RUN_TEST(test_disable);
    RUN_TEST(test_enable);
    RUN_TEST(test_serialize);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
