#include "test.h"
#include "nevercode.h"

static int t_init(void) {
    vtrig_init();
    ASSERT_INT_EQ(vtrig_distinct_files(), 0);
    ASSERT_INT_EQ(vtrig_total_edits(), 0);
    ASSERT_FALSE(vtrig_should_verify());
    ASSERT_INT_EQ(vtrig_threshold(), VTRIG_DEFAULT_THRESHOLD);
    return 0;
}

static int t_record_dedup(void) {
    vtrig_init();
    vtrig_record_edit("src/main.c");
    vtrig_record_edit("src/main.c");
    vtrig_record_edit("src/main.c");
    ASSERT_INT_EQ(vtrig_distinct_files(), 1);
    ASSERT_INT_EQ(vtrig_total_edits(), 3);
    ASSERT_FALSE(vtrig_should_verify());
    return 0;
}

static int t_trigger_at_threshold(void) {
    vtrig_init();
    vtrig_set_threshold(2);
    vtrig_record_edit("a.c");
    ASSERT_FALSE(vtrig_should_verify());
    vtrig_record_edit("b.c");
    ASSERT_TRUE(vtrig_should_verify());
    ASSERT_INT_EQ(vtrig_distinct_files(), 2);
    return 0;
}

static int t_acknowledge(void) {
    vtrig_init();
    vtrig_set_threshold(1);
    vtrig_record_edit("x.c");
    ASSERT_TRUE(vtrig_should_verify());
    vtrig_acknowledge();
    ASSERT_FALSE(vtrig_should_verify());
    return 0;
}

static int t_summary(void) {
    vtrig_init();
    vtrig_record_edit("foo.c");
    vtrig_record_edit("bar.c");
    char *s = vtrig_summary();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "foo.c");
    ASSERT_STR_CONTAINS(s, "bar.c");
    ASSERT_STR_CONTAINS(s, "2 distinct");
    free(s);
    return 0;
}

static int t_summary_empty(void) {
    vtrig_init();
    char *s = vtrig_summary();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "No file");
    free(s);
    return 0;
}

static int t_null_safety(void) {
    vtrig_init();
    vtrig_record_edit(NULL);
    vtrig_record_edit("");
    ASSERT_INT_EQ(vtrig_distinct_files(), 0);
    return 0;
}

static int t_reset(void) {
    vtrig_init();
    vtrig_record_edit("a.c");
    vtrig_record_edit("b.c");
    vtrig_record_edit("c.c");
    ASSERT_TRUE(vtrig_should_verify());
    vtrig_reset();
    ASSERT_INT_EQ(vtrig_distinct_files(), 0);
    ASSERT_FALSE(vtrig_should_verify());
    return 0;
}

static int t_macros(void) {
    ASSERT_GT(VTRIG_DEFAULT_THRESHOLD, 0);
    ASSERT_GT(VTRIG_MAX_FILES, 0);
    ASSERT_GT(VTRIG_PATH_MAX, 0);
    return 0;
}

int test_verify_trigger_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("verify_trigger");
    RUN_TEST(t_init);
    RUN_TEST(t_record_dedup);
    RUN_TEST(t_trigger_at_threshold);
    RUN_TEST(t_acknowledge);
    RUN_TEST(t_summary);
    RUN_TEST(t_summary_empty);
    RUN_TEST(t_null_safety);
    RUN_TEST(t_reset);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
