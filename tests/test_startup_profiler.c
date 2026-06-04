#include "test.h"
#include "nevercode.h"
#include <unistd.h>

static int t_init_empty(void) {
    sprof_init();
    ASSERT_INT_EQ(sprof_count(), 0);
    ASSERT_FALSE(sprof_enabled());
    return 0;
}

static int t_enable_and_checkpoint(void) {
    sprof_init();
    sprof_set_enabled(true);
    ASSERT_TRUE(sprof_enabled());
    sprof_checkpoint("phase_a");
    sprof_checkpoint("phase_b");
    ASSERT_INT_EQ(sprof_count(), 2);
    return 0;
}

static int t_disabled_no_record(void) {
    sprof_init();
    sprof_checkpoint("ignored");
    ASSERT_INT_EQ(sprof_count(), 0);
    return 0;
}

static int t_report(void) {
    sprof_init();
    sprof_set_enabled(true);
    sprof_checkpoint("start");
    sprof_checkpoint("end");
    char *r = sprof_report();
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "start");
    ASSERT_STR_CONTAINS(r, "end");
    ASSERT_STR_CONTAINS(r, "Total:");
    free(r);
    return 0;
}

static int t_report_empty(void) {
    sprof_init();
    char *r = sprof_report();
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "no checkpoints");
    free(r);
    return 0;
}

static int t_null_name(void) {
    sprof_init();
    sprof_set_enabled(true);
    sprof_checkpoint(NULL);
    ASSERT_INT_EQ(sprof_count(), 0);
    return 0;
}

static int t_total_ms(void) {
    sprof_init();
    sprof_set_enabled(true);
    sprof_checkpoint("a");
    sprof_checkpoint("b");
    long ms = sprof_total_ms();
    ASSERT_GE((int)ms, 0);
    return 0;
}

static int t_macros(void) {
    ASSERT_GT(SPROF_MAX_MARKS, 0);
    ASSERT_GT(SPROF_NAME_MAX, 0);
    ASSERT_NOT_NULL(SPROF_ENV_VAR);
    return 0;
}

int test_startup_profiler_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("startup_profiler");
    RUN_TEST(t_init_empty);
    RUN_TEST(t_enable_and_checkpoint);
    RUN_TEST(t_disabled_no_record);
    RUN_TEST(t_report);
    RUN_TEST(t_report_empty);
    RUN_TEST(t_null_name);
    RUN_TEST(t_total_ms);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
