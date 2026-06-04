#include "test.h"
#include "nevercode.h"
#include "terminal.h"

static int test_init(void) {
    chunk_policy_t cp;
    chunk_policy_init(&cp);
    ASSERT_INT_EQ((int)cp.mode, (int)CHUNK_SMOOTH);
    ASSERT_INT_EQ(cp.queued_lines, 0);
    return 0;
}

static int test_smooth_single_line(void) {
    chunk_policy_t cp;
    chunk_policy_init(&cp);
    int n = chunk_policy_drain_count(&cp, 1, 0);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ((int)cp.mode, (int)CHUNK_SMOOTH);
    return 0;
}

static int test_smooth_few_lines(void) {
    chunk_policy_t cp;
    chunk_policy_init(&cp);
    int n = chunk_policy_drain_count(&cp, 3, 10);
    ASSERT_INT_EQ(n, NC_CHUNK_SMOOTH_LINES);
    return 0;
}

static int test_enter_catchup_depth(void) {
    chunk_policy_t cp;
    chunk_policy_init(&cp);
    int n = chunk_policy_drain_count(&cp, NC_CHUNK_ENTER_DEPTH, 0);
    ASSERT_INT_EQ(n, NC_CHUNK_ENTER_DEPTH);
    ASSERT_INT_EQ((int)cp.mode, (int)CHUNK_CATCHUP);
    return 0;
}

static int test_enter_catchup_age(void) {
    chunk_policy_t cp;
    chunk_policy_init(&cp);
    int n = chunk_policy_drain_count(&cp, 3, NC_CHUNK_ENTER_AGE_MS);
    ASSERT_INT_EQ(n, 3);
    ASSERT_INT_EQ((int)cp.mode, (int)CHUNK_CATCHUP);
    return 0;
}

static int test_empty_queue(void) {
    chunk_policy_t cp;
    chunk_policy_init(&cp);
    int n = chunk_policy_drain_count(&cp, 0, 0);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

static int test_null_safety(void) {
    ASSERT_INT_EQ(chunk_policy_drain_count(NULL, 5, 10), 0);
    chunk_policy_init(NULL);
    return 0;
}

static int test_catchup_drains_all(void) {
    chunk_policy_t cp;
    chunk_policy_init(&cp);
    chunk_policy_drain_count(&cp, NC_CHUNK_ENTER_DEPTH + 5, 0);
    ASSERT_INT_EQ((int)cp.mode, (int)CHUNK_CATCHUP);
    int n = chunk_policy_drain_count(&cp, 20, 100);
    ASSERT_INT_EQ(n, 20);
    return 0;
}

int test_chunk_policy_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("chunk_policy");
    RUN_TEST(test_init);
    RUN_TEST(test_smooth_single_line);
    RUN_TEST(test_smooth_few_lines);
    RUN_TEST(test_enter_catchup_depth);
    RUN_TEST(test_enter_catchup_age);
    RUN_TEST(test_empty_queue);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_catchup_drains_all);
    TEST_RETURN_FAILED();
}
