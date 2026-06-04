#include "test.h"
#include "nevercode.h"

static int t_init_free(void) {
    key_pool_init();
    ASSERT_INT_EQ(key_pool_pool_count(), 0);
    ASSERT_INT_EQ(key_pool_total_rotations(), 0);
    key_pool_free();
    return 0;
}

static int t_create_pool(void) {
    key_pool_init();
    ASSERT_INT_EQ(key_pool_create("test", 0), 0);
    ASSERT_INT_EQ(key_pool_pool_count(), 1);
    ASSERT_INT_EQ(key_pool_create("test", 0), -1);
    key_pool_free();
    return 0;
}

static int t_add_keys(void) {
    key_pool_init();
    key_pool_create("p1", 0);
    ASSERT_INT_EQ(key_pool_add_key("p1", "key-a"), 0);
    ASSERT_INT_EQ(key_pool_add_key("p1", "key-b"), 0);
    ASSERT_INT_EQ(key_pool_total_count("p1"), 2);
    ASSERT_INT_EQ(key_pool_available_count("p1"), 2);
    key_pool_free();
    return 0;
}

static int t_round_robin(void) {
    key_pool_init();
    key_pool_create("rr", 0);
    key_pool_add_key("rr", "k1");
    key_pool_add_key("rr", "k2");
    key_pool_add_key("rr", "k3");

    ASSERT_STR_EQ(key_pool_next("rr"), "k1");
    ASSERT_STR_EQ(key_pool_next("rr"), "k2");
    ASSERT_STR_EQ(key_pool_next("rr"), "k3");
    ASSERT_STR_EQ(key_pool_next("rr"), "k1");
    key_pool_free();
    return 0;
}

static int t_least_used(void) {
    key_pool_init();
    key_pool_create("lu", 2);
    key_pool_add_key("lu", "a");
    key_pool_add_key("lu", "b");

    const char *x = key_pool_next("lu");
    ASSERT_NOT_NULL(x);
    const char *y = key_pool_next("lu");
    ASSERT_NOT_NULL(y);
    key_pool_free();
    return 0;
}

static int t_cooldown(void) {
    key_pool_init();
    key_pool_create("cd", 0);
    key_pool_add_key("cd", "hot");
    key_pool_add_key("cd", "cold");

    key_pool_report_failure("cd", "hot", 429);
    ASSERT_INT_EQ(key_pool_available_count("cd"), 1);

    ASSERT_STR_EQ(key_pool_next("cd"), "cold");

    key_pool_clear_cooldown("cd", "hot");
    ASSERT_INT_EQ(key_pool_available_count("cd"), 2);
    key_pool_free();
    return 0;
}

static int t_all_exhausted(void) {
    key_pool_init();
    key_pool_create("ex", 0);
    key_pool_add_key("ex", "only");
    key_pool_report_failure("ex", "only", 429);
    ASSERT_NULL(key_pool_next("ex"));
    key_pool_free();
    return 0;
}

static int t_auto_create_pool(void) {
    key_pool_init();
    ASSERT_INT_EQ(key_pool_add_key("auto", "k1"), 0);
    ASSERT_INT_EQ(key_pool_pool_count(), 1);
    ASSERT_INT_EQ(key_pool_total_count("auto"), 1);
    key_pool_free();
    return 0;
}

static int t_format(void) {
    key_pool_init();
    key_pool_add_key("fmt", "x");
    char *s = key_pool_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "fmt");
    free(s);
    key_pool_free();
    return 0;
}

static int t_config_load(void) {
    key_pool_init();
    cJSON *cfg = cJSON_Parse("{\"key_pools\":[{\"label\":\"p\",\"strategy\":\"random\","
                             "\"keys\":[\"a\",\"b\"]}]}");
    ASSERT_NOT_NULL(cfg);
    ASSERT_INT_EQ(key_pool_load_from_config(cfg), 1);
    ASSERT_INT_EQ(key_pool_total_count("p"), 2);
    cJSON_Delete(cfg);
    key_pool_free();
    return 0;
}

static int t_null_args(void) {
    key_pool_init();
    ASSERT_INT_EQ(key_pool_create(NULL, 0), -1);
    ASSERT_INT_EQ(key_pool_add_key(NULL, NULL), -1);
    ASSERT_NULL(key_pool_next(NULL));
    ASSERT_NULL(key_pool_next("missing"));
    ASSERT_INT_EQ(key_pool_available_count("missing"), 0);
    key_pool_free();
    return 0;
}

static int t_fill_first(void) {
    key_pool_init();
    key_pool_create("ff", 3);
    key_pool_add_key("ff", "primary");
    key_pool_add_key("ff", "secondary");
    key_pool_add_key("ff", "tertiary");

    ASSERT_STR_EQ(key_pool_next("ff"), "primary");
    ASSERT_STR_EQ(key_pool_next("ff"), "primary");
    ASSERT_STR_EQ(key_pool_next("ff"), "primary");

    key_pool_report_failure("ff", "primary", 429);
    ASSERT_STR_EQ(key_pool_next("ff"), "secondary");
    ASSERT_STR_EQ(key_pool_next("ff"), "secondary");

    key_pool_clear_cooldown("ff", "primary");
    ASSERT_STR_EQ(key_pool_next("ff"), "primary");
    key_pool_free();
    return 0;
}

static int t_least_used_prefers_lower_failure_pressure(void) {
    key_pool_init();
    key_pool_create("lu2", 2);
    key_pool_add_key("lu2", "a");
    key_pool_add_key("lu2", "b");

    ASSERT_STR_EQ(key_pool_next("lu2"), "a");
    ASSERT_STR_EQ(key_pool_next("lu2"), "b");

    key_pool_report_failure("lu2", "a", 500);
    key_pool_clear_cooldown("lu2", "a");
    ASSERT_STR_EQ(key_pool_next("lu2"), "b");

    key_pool_free();
    return 0;
}

static int t_failure_pressure_recovers_on_success(void) {
    key_pool_init();
    key_pool_create("rec", 3);
    key_pool_add_key("rec", "k");

    key_pool_report_failure("rec", "k", 500);
    key_pool_clear_cooldown("rec", "k");

    char *before = key_pool_format();
    ASSERT_NOT_NULL(before);
    ASSERT_STR_CONTAINS(before, "fails=1");
    free(before);

    ASSERT_STR_EQ(key_pool_next("rec"), "k");

    char *after = key_pool_format();
    ASSERT_NOT_NULL(after);
    ASSERT_STR_CONTAINS(after, "fails=0");
    free(after);

    key_pool_free();
    return 0;
}

static int t_config_fill_first(void) {
    key_pool_init();
    cJSON *cfg = cJSON_Parse("{\"key_pools\":[{\"label\":\"ff\",\"strategy\":\"fill_first\","
                             "\"keys\":[\"a\",\"b\"]}]}");
    ASSERT_NOT_NULL(cfg);
    ASSERT_INT_EQ(key_pool_load_from_config(cfg), 1);
    ASSERT_STR_EQ(key_pool_next("ff"), "a");
    ASSERT_STR_EQ(key_pool_next("ff"), "a");
    cJSON_Delete(cfg);
    key_pool_free();
    return 0;
}

int test_key_pool_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("key_pool");
    RUN_TEST(t_init_free);
    RUN_TEST(t_create_pool);
    RUN_TEST(t_add_keys);
    RUN_TEST(t_round_robin);
    RUN_TEST(t_least_used);
    RUN_TEST(t_cooldown);
    RUN_TEST(t_all_exhausted);
    RUN_TEST(t_auto_create_pool);
    RUN_TEST(t_format);
    RUN_TEST(t_config_load);
    RUN_TEST(t_null_args);
    RUN_TEST(t_fill_first);
    RUN_TEST(t_least_used_prefers_lower_failure_pressure);
    RUN_TEST(t_failure_pressure_recovers_on_success);
    RUN_TEST(t_config_fill_first);
    TEST_RETURN_FAILED();
}
