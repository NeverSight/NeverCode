/*
 * test_failure_router.c — Tests for failure classification and retry routing.
 */

#include "test.h"
#include "harness.h"
#include "defaults.h"

static int test_classify_transient_429(void) {
    ASSERT_INT_EQ(harness_classify_failure("rate_limit exceeded", 429), FAIL_TRANSIENT);
    ASSERT_INT_EQ(harness_classify_failure("Too Many Requests", 429), FAIL_TRANSIENT);
    return 0;
}

static int test_classify_transient_overloaded(void) {
    ASSERT_INT_EQ(harness_classify_failure("server overloaded", 503), FAIL_TRANSIENT);
    ASSERT_INT_EQ(harness_classify_failure("timeout waiting for response", 0), FAIL_TRANSIENT);
    ASSERT_INT_EQ(harness_classify_failure("ECONNRESET", 0), FAIL_TRANSIENT);
    return 0;
}

static int test_classify_resource(void) {
    ASSERT_INT_EQ(harness_classify_failure("prompt_too_long", 400), FAIL_RESOURCE);
    ASSERT_INT_EQ(harness_classify_failure("context_length exceeded", 400), FAIL_RESOURCE);
    ASSERT_INT_EQ(harness_classify_failure("max_tokens limit reached", 0), FAIL_RESOURCE);
    return 0;
}

static int test_classify_semantic(void) {
    ASSERT_INT_EQ(harness_classify_failure("invalid parameter", 400), FAIL_SEMANTIC);
    ASSERT_INT_EQ(harness_classify_failure("malformed request body", 400), FAIL_SEMANTIC);
    return 0;
}

static int test_classify_fatal(void) {
    ASSERT_INT_EQ(harness_classify_failure("auth failed", 401), FAIL_FATAL);
    ASSERT_INT_EQ(harness_classify_failure("permission denied", 403), FAIL_FATAL);
    ASSERT_INT_EQ(harness_classify_failure(NULL, 0), FAIL_FATAL);
    return 0;
}

static int test_classify_persistent(void) {
    ASSERT_INT_EQ(harness_classify_failure("internal server error", 500), FAIL_PERSISTENT);
    return 0;
}

static int test_failure_names(void) {
    ASSERT_STR_EQ(harness_failure_name(FAIL_TRANSIENT), "transient");
    ASSERT_STR_EQ(harness_failure_name(FAIL_PERSISTENT), "persistent");
    ASSERT_STR_EQ(harness_failure_name(FAIL_RESOURCE), "resource");
    ASSERT_STR_EQ(harness_failure_name(FAIL_SEMANTIC), "semantic");
    ASSERT_STR_EQ(harness_failure_name(FAIL_FATAL), "fatal");
    return 0;
}

static int test_should_retry(void) {
    ASSERT_TRUE(harness_should_retry(FAIL_TRANSIENT, 0));
    ASSERT_TRUE(harness_should_retry(FAIL_TRANSIENT, HARNESS_RETRY_TRANSIENT_MAX - 1));
    ASSERT_FALSE(harness_should_retry(FAIL_TRANSIENT, HARNESS_RETRY_TRANSIENT_MAX));
    ASSERT_FALSE(harness_should_retry(FAIL_FATAL, 0));
    ASSERT_TRUE(harness_should_retry(FAIL_SEMANTIC, 0));
    ASSERT_FALSE(harness_should_retry(FAIL_SEMANTIC, HARNESS_RETRY_SEMANTIC_MAX));
    return 0;
}

static int test_retry_delay(void) {
    failure_router_init();
    int d0 = harness_retry_delay_ms(FAIL_TRANSIENT, 0);
    ASSERT_GT(d0, 0);
    int d1 = harness_retry_delay_ms(FAIL_TRANSIENT, 1);
    ASSERT_GT(d1, d0 / 2);
    int d_fatal = harness_retry_delay_ms(FAIL_FATAL, 0);
    ASSERT_INT_EQ(d_fatal, 0);
    int d_big = harness_retry_delay_ms(FAIL_TRANSIENT, 20);
    ASSERT(d_big <= HARNESS_RETRY_MAX_DELAY);
    failure_router_free();
    return 0;
}

static int test_retry_delay_attempt_bounds(void) {
    failure_router_init();
    int d_neg = harness_retry_delay_ms(FAIL_TRANSIENT, -7);
    int d_zero = harness_retry_delay_ms(FAIL_TRANSIENT, 0);
    ASSERT_GT(d_neg, 0);
    ASSERT_GT(d_zero, 0);
    ASSERT(d_neg <= HARNESS_RETRY_MAX_DELAY);
    ASSERT(d_zero <= HARNESS_RETRY_MAX_DELAY);

    int d_huge = harness_retry_delay_ms(FAIL_TRANSIENT, 1000000);
    ASSERT_GT(d_huge, 0);
    ASSERT(d_huge <= HARNESS_RETRY_MAX_DELAY);
    failure_router_free();
    return 0;
}

static int test_retry_delay_burst_floor_for_transient(void) {
    failure_router_init();
    for (int i = 0; i < FR_WINDOW_SIZE; i++)
        harness_classify_failure("rate_limit", 429);
    ASSERT(harness_adaptive_scale(FAIL_TRANSIENT) >= FR_BURST_SCALE_THRESHOLD);

    int d = harness_retry_delay_ms(FAIL_TRANSIENT, 0);
    ASSERT(d >= HARNESS_RETRY_BASE_TRANSIENT * FR_BURST_MIN_MULTIPLIER);
    ASSERT(d <= HARNESS_RETRY_MAX_DELAY);
    failure_router_free();
    return 0;
}

static int test_init_free(void) {
    failure_router_init();
    ASSERT_INT_EQ(harness_failure_window_count(FAIL_TRANSIENT), 0);
    failure_router_free();
    return 0;
}

static int test_adaptive_scale_default(void) {
    failure_router_init();
    double s = harness_adaptive_scale(FAIL_TRANSIENT);
    ASSERT(s >= 1.0 && s <= 1.01);
    failure_router_free();
    return 0;
}

static int test_adaptive_scale_increases(void) {
    failure_router_init();
    for (int i = 0; i < 5; i++)
        harness_classify_failure("rate_limit", 429);
    double s = harness_adaptive_scale(FAIL_TRANSIENT);
    ASSERT(s > 1.0);
    ASSERT(harness_failure_window_count(FAIL_TRANSIENT) == 5);
    failure_router_free();
    return 0;
}

static int test_window_count(void) {
    failure_router_init();
    ASSERT_INT_EQ(harness_failure_window_count(FAIL_RESOURCE), 0);
    harness_classify_failure("prompt_too_long", 400);
    ASSERT_INT_EQ(harness_failure_window_count(FAIL_RESOURCE), 1);
    failure_router_free();
    return 0;
}

static int test_snapshot_restore(void) {
    failure_router_init();
    for (int i = 0; i < 4; i++)
        harness_classify_failure("rate_limit", 429);
    harness_classify_failure("prompt_too_long", 400);

    failure_router_snapshot_t snap;
    failure_router_snapshot(&snap);
    failure_router_free();

    failure_router_init();
    failure_router_restore(&snap);
    ASSERT_INT_EQ(harness_failure_window_count(FAIL_TRANSIENT), 4);
    ASSERT_INT_EQ(harness_failure_window_count(FAIL_RESOURCE), 1);
    ASSERT(harness_adaptive_scale(FAIL_TRANSIENT) > 1.0);

    harness_classify_failure("rate_limit", 429);
    ASSERT_INT_EQ(harness_failure_window_count(FAIL_TRANSIENT), 5);
    failure_router_free();
    return 0;
}

int test_failure_router_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("failure_router");
    RUN_TEST(test_classify_transient_429);
    RUN_TEST(test_classify_transient_overloaded);
    RUN_TEST(test_classify_resource);
    RUN_TEST(test_classify_semantic);
    RUN_TEST(test_classify_fatal);
    RUN_TEST(test_classify_persistent);
    RUN_TEST(test_failure_names);
    RUN_TEST(test_should_retry);
    RUN_TEST(test_retry_delay);
    RUN_TEST(test_retry_delay_attempt_bounds);
    RUN_TEST(test_retry_delay_burst_floor_for_transient);
    RUN_TEST(test_init_free);
    RUN_TEST(test_adaptive_scale_default);
    RUN_TEST(test_adaptive_scale_increases);
    RUN_TEST(test_window_count);
    RUN_TEST(test_snapshot_restore);
    TEST_RETURN_FAILED();
}
