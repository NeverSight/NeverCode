#include "test.h"
#include "nevercode.h"

void tsc_init(void);
void tsc_clear(void);
const char *tsc_get(const char *name);
int tsc_put(const char *name, const char *schema_json);
int tsc_invalidate(const char *name);
void tsc_invalidate_all(void);
int tsc_count(void);
char *tsc_format_stats(void);

static int test_init(void) {
    tsc_init();
    ASSERT_INT_EQ(tsc_count(), 0);
    return 0;
}

static int test_put_get(void) {
    tsc_init();
    ASSERT_INT_EQ(tsc_put("Bash", "{\"type\":\"object\"}"), 0);
    ASSERT_INT_EQ(tsc_count(), 1);

    const char *r = tsc_get("Bash");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "{\"type\":\"object\"}");

    tsc_clear();
    return 0;
}

static int test_miss(void) {
    tsc_init();
    ASSERT_NULL(tsc_get("nonexistent"));
    tsc_clear();
    return 0;
}

static int test_invalidate(void) {
    tsc_init();
    tsc_put("Read", "{\"type\":\"object\"}");
    ASSERT_INT_EQ(tsc_count(), 1);

    tsc_invalidate("Read");
    ASSERT_NULL(tsc_get("Read"));

    tsc_clear();
    return 0;
}

static int test_invalidate_all(void) {
    tsc_init();
    tsc_put("a", "{}");
    tsc_put("b", "{}");
    tsc_put("c", "{}");
    ASSERT_INT_EQ(tsc_count(), 3);

    tsc_invalidate_all();
    ASSERT_INT_EQ(tsc_count(), 0);
    return 0;
}

static int test_overwrite(void) {
    tsc_init();
    tsc_put("Grep", "{\"v1\":true}");
    tsc_put("Grep", "{\"v2\":true}");
    ASSERT_INT_EQ(tsc_count(), 1);
    ASSERT_STR_EQ(tsc_get("Grep"), "{\"v2\":true}");
    tsc_clear();
    return 0;
}

static int test_format_stats(void) {
    tsc_init();
    tsc_put("X", "{}");
    tsc_get("X");
    tsc_get("Y"); /* miss */

    char *s = tsc_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Schema cache:");
    free(s);
    tsc_clear();
    return 0;
}

static int test_null_args(void) {
    tsc_init();
    ASSERT_INT_EQ(tsc_put(NULL, "{}"), -1);
    ASSERT_INT_EQ(tsc_put("x", NULL), -1);
    ASSERT_NULL(tsc_get(NULL));
    tsc_clear();
    return 0;
}

int test_tool_schema_cache_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_schema_cache");
    RUN_TEST(test_init);
    RUN_TEST(test_put_get);
    RUN_TEST(test_miss);
    RUN_TEST(test_invalidate);
    RUN_TEST(test_invalidate_all);
    RUN_TEST(test_overwrite);
    RUN_TEST(test_format_stats);
    RUN_TEST(test_null_args);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_schema_cache");
    RUN_TEST(test_init);
    RUN_TEST(test_put_get);
    RUN_TEST(test_miss);
    RUN_TEST(test_invalidate);
    RUN_TEST(test_invalidate_all);
    RUN_TEST(test_overwrite);
    RUN_TEST(test_format_stats);
    RUN_TEST(test_null_args);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
