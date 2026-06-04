#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init_defaults(void) {
    retry_cycle_t rc;
    rcycle_init(&rc, 0);
    ASSERT_INT_EQ(rc.max_iterations, RCYCLE_DEFAULT_MAX_ITERS);
    ASSERT_INT_EQ(rc.nedges, 0);
    rcycle_free(&rc);
    return 0;
}

static int t_add_edge(void) {
    retry_cycle_t rc;
    rcycle_init(&rc, 3);
    const char *targets[] = {"build", "test"};
    ASSERT_INT_EQ(rcycle_add_edge(&rc, "verify", targets, 2), 0);
    ASSERT_INT_EQ(rc.nedges, 1);
    rcycle_free(&rc);
    return 0;
}

static int t_should_restart(void) {
    retry_cycle_t rc;
    rcycle_init(&rc, 2);
    const char *targets[] = {"step1"};
    rcycle_add_edge(&rc, "step2", targets, 1);

    ASSERT_TRUE(rcycle_should_restart(&rc, "step2"));
    ASSERT_FALSE(rcycle_should_restart(&rc, "unknown"));
    rcycle_free(&rc);
    return 0;
}

static int t_iteration_count(void) {
    retry_cycle_t rc;
    rcycle_init(&rc, 5);
    const char *targets[] = {"a"};
    rcycle_add_edge(&rc, "b", targets, 1);

    ASSERT_INT_EQ(rcycle_iteration(&rc, "b"), 0);
    ASSERT_FALSE(rcycle_exhausted(&rc, "b"));
    rcycle_free(&rc);
    return 0;
}

static int t_exhaustion(void) {
    retry_cycle_t rc;
    rcycle_init(&rc, 1);
    const char *targets[] = {"x"};
    rcycle_add_edge(&rc, "y", targets, 1);

    rc.iteration_counts[0] = 1;
    ASSERT_TRUE(rcycle_exhausted(&rc, "y"));
    ASSERT_FALSE(rcycle_should_restart(&rc, "y"));
    rcycle_free(&rc);
    return 0;
}

static int t_unknown_tail_exhausted(void) {
    retry_cycle_t rc;
    rcycle_init(&rc, 5);
    ASSERT_TRUE(rcycle_exhausted(&rc, "nonexistent"));
    ASSERT_INT_EQ(rcycle_iteration(&rc, "nonexistent"), 0);
    rcycle_free(&rc);
    return 0;
}

static int t_null_rejected(void) {
    retry_cycle_t rc;
    rcycle_init(&rc, 3);
    ASSERT_INT_EQ(rcycle_add_edge(&rc, NULL, NULL, 0), -1);
    rcycle_free(&rc);
    return 0;
}

static int t_format_status(void) {
    retry_cycle_t rc;
    rcycle_init(&rc, 3);
    const char *targets[] = {"src"};
    rcycle_add_edge(&rc, "dest", targets, 1);

    char *s = rcycle_format_status(&rc);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "dest");
    ASSERT_STR_CONTAINS(s, "src");
    ASSERT_STR_CONTAINS(s, "max_iterations=3");
    free(s);
    rcycle_free(&rc);
    return 0;
}

int test_retry_cycle_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("retry_cycle");
    RUN_TEST(t_init_defaults);
    RUN_TEST(t_add_edge);
    RUN_TEST(t_should_restart);
    RUN_TEST(t_iteration_count);
    RUN_TEST(t_exhaustion);
    RUN_TEST(t_unknown_tail_exhausted);
    RUN_TEST(t_null_rejected);
    RUN_TEST(t_format_status);
    TEST_RETURN_FAILED();
}
