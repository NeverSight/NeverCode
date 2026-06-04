#include "test.h"
#include "nevercode.h"
#include "harness.h"
#include "defaults.h"

static int test_classify_rate_limit(void) {
    ASSERT_INT_EQ(api_retry_classify("429 Too Many Requests"), 1);
    ASSERT_INT_EQ(api_retry_classify("rate_limit exceeded"), 1);
    return 0;
}

static int test_classify_overloaded(void) {
    ASSERT_INT_EQ(api_retry_classify("529 overloaded"), 2);
    ASSERT_INT_EQ(api_retry_classify("503 Service Unavailable"), 2);
    return 0;
}

static int test_classify_server_error(void) {
    ASSERT_INT_EQ(api_retry_classify("500 Internal Server Error"), 3);
    ASSERT_INT_EQ(api_retry_classify("HTTP 502: error code: 502"), 3);
    ASSERT_INT_EQ(api_retry_classify("Bad Gateway"), 3);
    return 0;
}

static int test_classify_network(void) {
    ASSERT_INT_EQ(api_retry_classify("connection refused"), 4);
    ASSERT_INT_EQ(api_retry_classify("CURLE_COULDNT_CONNECT"), 4);
    ASSERT_INT_EQ(api_retry_classify("timeout error"), 4);
    return 0;
}

static int test_classify_auth(void) {
    ASSERT_INT_EQ(api_retry_classify("401 unauthorized"), 5);
    ASSERT_INT_EQ(api_retry_classify("403 forbidden"), 5);
    return 0;
}

static int test_classify_none(void) {
    ASSERT_INT_EQ(api_retry_classify("something else"), 0);
    ASSERT_INT_EQ(api_retry_classify(NULL), 0);
    return 0;
}

static int test_should_retry_yes(void) {
    ASSERT(api_retry_should_retry(1) == true); /* rate_limit */
    ASSERT(api_retry_should_retry(2) == true); /* overloaded */
    ASSERT(api_retry_should_retry(3) == true); /* server_err */
    ASSERT(api_retry_should_retry(4) == true); /* network */
    return 0;
}

static int test_should_retry_no(void) {
    ASSERT(api_retry_should_retry(0) == false); /* none */
    ASSERT(api_retry_should_retry(5) == false); /* auth */
    ASSERT(api_retry_should_retry(6) == false); /* bad_request */
    return 0;
}

static int test_delay_increases(void) {
    int d0 = api_retry_delay_ms(0, 3);
    int d1 = api_retry_delay_ms(1, 3);
    int d2 = api_retry_delay_ms(2, 3);
    ASSERT(d0 > 0);
    ASSERT(d1 > d0 / 2);
    ASSERT(d2 > d1 / 2);
    return 0;
}

static int test_delay_rate_limit_higher(void) {
    int d_normal = api_retry_delay_ms(0, 3);
    int d_rl = api_retry_delay_ms(0, 1);
    ASSERT(d_rl > d_normal);
    return 0;
}

static int test_delay_overloaded_highest(void) {
    int d_rl = api_retry_delay_ms(0, 1);
    int d_ol = api_retry_delay_ms(0, 2);
    ASSERT(d_ol > d_rl);
    return 0;
}

static int test_delay_attempt_bounds(void) {
    int d_neg = api_retry_delay_ms(-5, 3);
    ASSERT(d_neg > 0);
    ASSERT(d_neg <= RETRY_MAX_DELAY_MS);

    int d_huge = api_retry_delay_ms(999999, 1);
    ASSERT(d_huge > 0);
    ASSERT(d_huge <= RETRY_MAX_DELAY_MS);

    int d_huge2 = api_retry_delay_ms(999999, 2);
    ASSERT(d_huge2 > 0);
    ASSERT(d_huge2 <= RETRY_MAX_DELAY_MS);
    return 0;
}

static int test_harness_bridge(void) {
    failure_router_init();
    ASSERT_INT_EQ(harness_failure_window_count(FAIL_TRANSIENT), 0);

    int cls = api_retry_classify("429 Too Many Requests");
    (void)cls;

    ASSERT(harness_failure_window_count(FAIL_TRANSIENT) == 0);
    failure_router_free();
    return 0;
}

int test_api_retry_all(void) {
    TEST_SUITE("api_retry");
    TEST_COUNTERS;

    RUN_TEST(test_classify_rate_limit);
    RUN_TEST(test_classify_overloaded);
    RUN_TEST(test_classify_server_error);
    RUN_TEST(test_classify_network);
    RUN_TEST(test_classify_auth);
    RUN_TEST(test_classify_none);
    RUN_TEST(test_should_retry_yes);
    RUN_TEST(test_should_retry_no);
    RUN_TEST(test_delay_increases);
    RUN_TEST(test_delay_rate_limit_higher);
    RUN_TEST(test_delay_overloaded_highest);
    RUN_TEST(test_delay_attempt_bounds);
    RUN_TEST(test_harness_bridge);

    TEST_RETURN_FAILED();
}
