#include "test.h"
#include "nevercode.h"
static int gi = 0, gu = 0, gh = 0;
static void *mi(void) {
    gi++;
    return (void *)42;
}
static void mu(void *c) {
    gu++;
    (void)c;
}
static int mh(plugin_event_t ev, const char *d, char **r, void *c) {
    (void)d;
    (void)r;
    (void)c;
    gh++;
    return ev == PLUGIN_ON_STARTUP ? 1 : 0;
}
static int t1(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    ASSERT_INT_EQ(r.count, 0);
    ASSERT_TRUE(r.enabled);
    plugin_registry_free(&r);
    return 0;
}
static int t2(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    ASSERT_INT_EQ(plugin_register(&r, "p", mi, mu, mh), 0);
    ASSERT_INT_EQ(plugin_count(&r), 1);
    ASSERT_INT_EQ(plugin_register(&r, "p", mi, mu, mh), -1);
    plugin_registry_free(&r);
    return 0;
}
static int t3(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    gi = gu = 0;
    plugin_register(&r, "l", mi, mu, mh);
    ASSERT_INT_EQ(plugin_load(&r, "l"), 0);
    ASSERT_INT_EQ(gi, 1);
    ASSERT_INT_EQ(plugin_loaded_count(&r), 1);
    ASSERT_INT_EQ(plugin_unload(&r, "l"), 0);
    ASSERT_INT_EQ(gu, 1);
    plugin_registry_free(&r);
    return 0;
}
static int t4(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    gh = 0;
    plugin_register(&r, "d", mi, mu, mh);
    plugin_load(&r, "d");
    ASSERT_INT_EQ(plugin_dispatch(&r, PLUGIN_ON_STARTUP, NULL, NULL), 1);
    ASSERT_INT_EQ(gh, 1);
    plugin_registry_free(&r);
    return 0;
}
static int t5(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    gh = 0;
    plugin_register(&r, "x", mi, mu, mh);
    plugin_load(&r, "x");
    r.enabled = false;
    ASSERT_INT_EQ(plugin_dispatch(&r, PLUGIN_ON_STARTUP, NULL, NULL), 0);
    plugin_registry_free(&r);
    return 0;
}
static int t6(void) {
    ASSERT_STR_EQ(plugin_state_name(PLUGIN_LOADED), "loaded");
    ASSERT_STR_EQ(plugin_event_name(PLUGIN_ON_STARTUP), "startup");
    return 0;
}
static int t7(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    char *s = plugin_format_list(&r);
    ASSERT_STR_CONTAINS(s, "No plugins");
    free(s);
    plugin_register(&r, "f", mi, mu, mh);
    plugin_load(&r, "f");
    s = plugin_format_list(&r);
    ASSERT_STR_CONTAINS(s, "f");
    free(s);
    plugin_registry_free(&r);
    return 0;
}
static int t8(void) {
    ASSERT_NOT_NULL(plugin_registry_global());
    return 0;
}
static int t9(void) {
    ASSERT_INT_EQ(plugin_register(NULL, "x", NULL, NULL, NULL), -1);
    ASSERT_INT_EQ(plugin_count(NULL), 0);
    return 0;
}
static int ta(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    ASSERT_INT_EQ(plugin_load(&r, "no"), -1);
    ASSERT_INT_EQ(plugin_load_so(&r, "/no.so"), -1);
    plugin_registry_free(&r);
    return 0;
}
static int tb(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    ASSERT_GE(plugin_load_dir(&r, "/tmp"), 0);
    plugin_registry_free(&r);
    return 0;
}
static void *nil_init(void) {
    return NULL;
}
static int tc(void) {
    plugin_registry_t r;
    plugin_registry_init(&r);
    plugin_register(&r, "nil", nil_init, NULL, NULL);
    ASSERT_INT_EQ(plugin_load(&r, "nil"), 0);
    ASSERT_INT_EQ(plugin_loaded_count(&r), 1);
    plugin_registry_free(&r);
    return 0;
}
int test_plugin_all(void) {
    TEST_SUITE("plugin");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(ta);
    RUN_TEST(tb);
    RUN_TEST(tc);
    TEST_RETURN_FAILED();
}
