#include "test.h"
#include "nevercode.h"

static int test_init_and_free(void) {
    coordinator_mode_init();
    ASSERT_INT_EQ(coordinator_mode_task_count(), 0);
    ASSERT_INT_EQ(coordinator_mode_completed(), 0);
    ASSERT_INT_EQ(coordinator_mode_failed(), 0);
    coordinator_mode_free();
    return 0;
}

static int test_plan_sets_goal(void) {
    coordinator_mode_init();
    ASSERT_INT_EQ(coordinator_mode_plan("solve the challenge"), 0);
    coordinator_mode_free();
    return 0;
}

static int test_add_tasks(void) {
    coordinator_mode_init();
    coordinator_mode_plan("goal");
    ASSERT_INT_EQ(coordinator_mode_add_task("alpha", 1, -1), 0);
    ASSERT_INT_EQ(coordinator_mode_add_task("beta", 2, 0), 1);
    ASSERT_INT_EQ(coordinator_mode_task_count(), 2);
    ASSERT_INT_EQ(coordinator_mode_pending(), 2);
    coordinator_mode_free();
    return 0;
}

static int test_reset_clears(void) {
    coordinator_mode_init();
    coordinator_mode_plan("goal");
    coordinator_mode_add_task("t", 1, -1);
    ASSERT_INT_EQ(coordinator_mode_task_count(), 1);
    coordinator_mode_reset();
    ASSERT_INT_EQ(coordinator_mode_task_count(), 0);
    coordinator_mode_free();
    return 0;
}

static int test_format(void) {
    coordinator_mode_init();
    coordinator_mode_plan("test goal");
    coordinator_mode_add_task("sub-1", 1, -1);
    char *f = coordinator_mode_format();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Coordinator:");
    ASSERT_STR_CONTAINS(f, "test goal");
    ASSERT_STR_CONTAINS(f, "sub-1");
    free(f);
    coordinator_mode_free();
    return 0;
}

static int test_no_init_returns_error(void) {
    coordinator_mode_free();
    ASSERT_INT_EQ(coordinator_mode_plan("g"), -1);
    return 0;
}

static int test_merge_no_tasks(void) {
    coordinator_mode_init();
    coordinator_mode_plan("empty plan");
    char *m = coordinator_mode_merge();
    ASSERT_NOT_NULL(m);
    ASSERT_STR_CONTAINS(m, "empty plan");
    ASSERT_STR_CONTAINS(m, "0/0 done");
    free(m);
    coordinator_mode_free();
    return 0;
}

int test_coordinator_mode_all(void) {
    TEST_SUITE("coordinator_mode");
    TEST_COUNTERS;
    RUN_TEST(test_init_and_free);
    RUN_TEST(test_plan_sets_goal);
    RUN_TEST(test_add_tasks);
    RUN_TEST(test_reset_clears);
    RUN_TEST(test_format);
    RUN_TEST(test_no_init_returns_error);
    RUN_TEST(test_merge_no_tasks);
    TEST_RETURN_FAILED();
}
