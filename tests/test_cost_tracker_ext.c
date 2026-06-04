#include "test.h"
#include "nevercode.h"

static int t_ext_tracking(void) {
    cost_tracker_init();
    cost_tracker_add_ext("test-model", 1000, 500, 200, 100, 50, 1234);

    ASSERT_INT_EQ(cost_tracker_total_input(), 1000);
    ASSERT_INT_EQ(cost_tracker_total_output(), 500);
    ASSERT_INT_EQ(cost_tracker_total_cache_read(), 200);
    ASSERT_INT_EQ(cost_tracker_total_cache_write(), 100);
    ASSERT_INT_EQ(cost_tracker_total_reasoning(), 50);
    ASSERT_INT_EQ(cost_tracker_total_requests(), 1);
    return 0;
}

static int t_retry_counter(void) {
    cost_tracker_init();
    ASSERT_INT_EQ(cost_tracker_total_retries(), 0);
    cost_tracker_add_retry();
    cost_tracker_add_retry();
    ASSERT_INT_EQ(cost_tracker_total_retries(), 2);
    return 0;
}

static int t_backward_compat(void) {
    cost_tracker_init();
    cost_tracker_add("compat-model", 500, 250, 100);
    ASSERT_INT_EQ(cost_tracker_total_input(), 500);
    ASSERT_INT_EQ(cost_tracker_total_output(), 250);
    ASSERT_INT_EQ(cost_tracker_total_cache_read(), 0);
    ASSERT_INT_EQ(cost_tracker_total_cache_write(), 0);
    ASSERT_INT_EQ(cost_tracker_total_reasoning(), 0);
    ASSERT_INT_EQ(cost_tracker_total_requests(), 1);
    return 0;
}

static int t_format_extended(void) {
    cost_tracker_init();
    cost_tracker_add_ext("model-a", 5000, 2000, 1000, 500, 300, 2000);
    cost_tracker_add_ext("model-b", 3000, 1000, 0, 0, 0, 1500);
    cost_tracker_add_retry();

    char *fmt = cost_tracker_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "Cache tokens:");
    ASSERT_STR_CONTAINS(fmt, "Reasoning tokens:");
    ASSERT_STR_CONTAINS(fmt, "API requests:");
    free(fmt);
    return 0;
}

static int t_no_cache_no_display(void) {
    cost_tracker_init();
    cost_tracker_add("plain-model", 1000, 500, 100);
    char *fmt = cost_tracker_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_NULL(strstr(fmt, "Cache tokens:"));
    ASSERT_NULL(strstr(fmt, "Reasoning tokens:"));
    free(fmt);
    return 0;
}

int test_cost_tracker_ext_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("cost_tracker_ext");
    RUN_TEST(t_ext_tracking);
    RUN_TEST(t_retry_counter);
    RUN_TEST(t_backward_compat);
    RUN_TEST(t_format_extended);
    RUN_TEST(t_no_cache_no_display);
    TEST_RETURN_FAILED();
}
