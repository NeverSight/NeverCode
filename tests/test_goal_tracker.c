/*
 * test_goal_tracker.c — Tests for goal evolution and tracking.
 */

#include "test.h"
#include "harness.h"
#include "defaults.h"

static int test_init(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    ASSERT_INT_EQ(g.count, 0);
    ASSERT_INT_EQ(g.active_idx, -1);
    ASSERT_NULL(goal_tracker_current(&g));
    goal_tracker_free(&g);
    return 0;
}

static int test_push_activates_first(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    int idx = goal_tracker_push(&g, "build the project", 1);
    ASSERT_INT_EQ(idx, 0);
    ASSERT_INT_EQ(g.count, 1);
    ASSERT_INT_EQ(g.active_idx, 0);
    ASSERT_STR_EQ(goal_tracker_current(&g), "build the project");
    ASSERT_INT_EQ(g.goals[0].status, GOAL_ACTIVE);
    goal_tracker_free(&g);
    return 0;
}

static int test_push_second_stays_pending(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "first", 1);
    goal_tracker_push(&g, "second", 2);
    ASSERT_INT_EQ(g.count, 2);
    ASSERT_INT_EQ(g.goals[1].status, GOAL_PENDING);
    ASSERT_STR_EQ(goal_tracker_current(&g), "first");
    goal_tracker_free(&g);
    return 0;
}

static int test_complete_promotes_next(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "alpha", 1);
    goal_tracker_push(&g, "beta", 2);
    goal_tracker_complete(&g, 0, "done");
    ASSERT_INT_EQ(g.goals[0].status, GOAL_COMPLETED);
    ASSERT_STR_EQ(g.goals[0].result, "done");
    ASSERT_INT_EQ(g.active_idx, 1);
    ASSERT_INT_EQ(g.goals[1].status, GOAL_ACTIVE);
    goal_tracker_free(&g);
    return 0;
}

static int test_fail_promotes_next(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "risky", 1);
    goal_tracker_push(&g, "fallback", 2);
    goal_tracker_fail(&g, 0, "crashed");
    ASSERT_INT_EQ(g.goals[0].status, GOAL_FAILED);
    ASSERT_STR_EQ(g.goals[0].result, "crashed");
    ASSERT_INT_EQ(g.active_idx, 1);
    goal_tracker_free(&g);
    return 0;
}

static int test_counts(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "a", 1);
    goal_tracker_push(&g, "b", 1);
    goal_tracker_push(&g, "c", 1);
    ASSERT_INT_EQ(goal_tracker_pending_count(&g), 3);
    ASSERT_INT_EQ(goal_tracker_completed_count(&g), 0);
    goal_tracker_complete(&g, 0, NULL);
    ASSERT_INT_EQ(goal_tracker_pending_count(&g), 2);
    ASSERT_INT_EQ(goal_tracker_completed_count(&g), 1);
    goal_tracker_free(&g);
    return 0;
}

static int test_status_names(void) {
    ASSERT_STR_EQ(goal_status_name(GOAL_PENDING), "pending");
    ASSERT_STR_EQ(goal_status_name(GOAL_ACTIVE), "active");
    ASSERT_STR_EQ(goal_status_name(GOAL_COMPLETED), "completed");
    ASSERT_STR_EQ(goal_status_name(GOAL_FAILED), "failed");
    ASSERT_STR_EQ(goal_status_name(GOAL_EVOLVED), "evolved");
    return 0;
}

static int test_format(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "solve CTF", 1);
    char *fmt = goal_tracker_format_status(&g);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "solve CTF");
    ASSERT_STR_CONTAINS(fmt, "1 total");
    free(fmt);
    goal_tracker_free(&g);
    return 0;
}

static int test_overflow(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    for (int i = 0; i < HARNESS_MAX_GOAL_DEPTH; i++)
        ASSERT_GE(goal_tracker_push(&g, "goal", 0), 0);
    ASSERT_INT_EQ(goal_tracker_push(&g, "overflow", 0), -1);
    goal_tracker_free(&g);
    return 0;
}

static int test_evolve_splits_goal(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "build feature X", 1);
    ASSERT_INT_EQ(g.active_idx, 0);

    const char *subs[] = {"design API", "implement logic", "write tests"};
    int rc = goal_tracker_evolve(&g, 0, subs, 3);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(g.goals[0].status, GOAL_EVOLVED);
    ASSERT_INT_EQ(g.count, 4);
    ASSERT_INT_EQ(g.active_idx, 1);
    ASSERT_STR_EQ(g.goals[1].description, "design API");
    ASSERT_INT_EQ(g.goals[1].status, GOAL_ACTIVE);
    ASSERT_INT_EQ(g.goals[2].status, GOAL_PENDING);
    ASSERT_INT_EQ(g.goals[3].status, GOAL_PENDING);
    goal_tracker_free(&g);
    return 0;
}

static int test_evolve_invalid_idx(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "main", 1);
    const char *subs[] = {"sub1"};
    ASSERT_INT_EQ(goal_tracker_evolve(&g, -1, subs, 1), -1);
    ASSERT_INT_EQ(goal_tracker_evolve(&g, 5, subs, 1), -1);
    ASSERT_INT_EQ(goal_tracker_evolve(&g, 0, NULL, 1), -1);
    ASSERT_INT_EQ(goal_tracker_evolve(&g, 0, subs, 0), -1);
    goal_tracker_free(&g);
    return 0;
}

static int test_active_idx_accessor(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    ASSERT_INT_EQ(goal_tracker_active_idx(&g), -1);
    goal_tracker_push(&g, "x", 1);
    ASSERT_INT_EQ(goal_tracker_active_idx(&g), 0);
    goal_tracker_free(&g);
    return 0;
}

static int test_count_accessor(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    ASSERT_INT_EQ(goal_tracker_count(&g), 0);
    goal_tracker_push(&g, "a", 1);
    goal_tracker_push(&g, "b", 1);
    ASSERT_INT_EQ(goal_tracker_count(&g), 2);
    goal_tracker_free(&g);
    return 0;
}

int test_goal_tracker_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("goal_tracker");
    RUN_TEST(test_init);
    RUN_TEST(test_push_activates_first);
    RUN_TEST(test_push_second_stays_pending);
    RUN_TEST(test_complete_promotes_next);
    RUN_TEST(test_fail_promotes_next);
    RUN_TEST(test_counts);
    RUN_TEST(test_status_names);
    RUN_TEST(test_format);
    RUN_TEST(test_overflow);
    RUN_TEST(test_evolve_splits_goal);
    RUN_TEST(test_evolve_invalid_idx);
    RUN_TEST(test_active_idx_accessor);
    RUN_TEST(test_count_accessor);
    TEST_RETURN_FAILED();
}
