#include "test.h"
#include "nevercode.h"
#include <math.h>

static int test_cost_init(void) {
    cost_tracker_init();
    ASSERT_INT_EQ(cost_tracker_total_input(), 0);
    ASSERT_INT_EQ(cost_tracker_total_output(), 0);
    ASSERT(cost_tracker_total_cost() < 0.0001);
    return 0;
}

static int test_cost_add_single(void) {
    cost_tracker_init();
    cost_tracker_add("gpt-5.4", 1000, 500, 200);
    ASSERT_INT_EQ(cost_tracker_total_input(), 1000);
    ASSERT_INT_EQ(cost_tracker_total_output(), 500);
    double cost = cost_tracker_total_cost();
    ASSERT(cost > 0.0);
    /* gpt-5: $10/M in, $30/M out -> 1000*10/1M + 500*30/1M = 0.025 */
    ASSERT(fabs(cost - 0.025) < 0.001);
    return 0;
}

static int test_cost_add_multi_model(void) {
    cost_tracker_init();
    cost_tracker_add("gpt-5.4", 1000, 500, 100);
    cost_tracker_add("gpt-4o-mini", 2000, 1000, 50);
    ASSERT_INT_EQ(cost_tracker_total_input(), 3000);
    ASSERT_INT_EQ(cost_tracker_total_output(), 1500);
    double cost = cost_tracker_total_cost();
    ASSERT(cost > 0.025);
    return 0;
}

static int test_cost_format(void) {
    cost_tracker_init();
    cost_tracker_add("gpt-5.4", 10000, 5000, 1500);
    char *f = cost_tracker_format();
    ASSERT_NOT_NULL(f);
    ASSERT(strstr(f, "Total cost") != NULL);
    ASSERT(strstr(f, "gpt-5.4") != NULL);
    free(f);
    return 0;
}

static int test_cost_format_empty(void) {
    cost_tracker_init();
    char *f = cost_tracker_format();
    ASSERT_NOT_NULL(f);
    ASSERT(strstr(f, "$0") != NULL);
    free(f);
    return 0;
}

static int test_cost_lines_tracking(void) {
    cost_tracker_init();
    cost_tracker_add_lines(10, 5);
    cost_tracker_add_lines(20, 3);
    char *f = cost_tracker_format();
    ASSERT_NOT_NULL(f);
    ASSERT(strstr(f, "30") != NULL);
    free(f);
    return 0;
}

static int test_cost_alt_provider_model(void) {
    cost_tracker_init();
    cost_tracker_add("claude-sonnet-4-20250514", 10000, 5000, 500);
    double cost = cost_tracker_total_cost();
    /* sonnet: $3/M in, $15/M out -> 10k*3/1M + 5k*15/1M = 0.03+0.075 = 0.105 */
    ASSERT(fabs(cost - 0.105) < 0.01);
    return 0;
}

static int test_cost_accumulate(void) {
    cost_tracker_init();
    cost_tracker_add("gpt-5.4", 500, 200, 100);
    cost_tracker_add("gpt-5.4", 500, 300, 100);
    ASSERT_INT_EQ(cost_tracker_total_input(), 1000);
    ASSERT_INT_EQ(cost_tracker_total_output(), 500);
    char *f = cost_tracker_format();
    ASSERT_NOT_NULL(f);
    ASSERT(strstr(f, "gpt-5.4") != NULL);
    free(f);
    return 0;
}

static int test_cost_ext_cache_tokens(void) {
    cost_tracker_init();
    cost_tracker_add_ext("gpt-5.4", 5000, 2000, 3000, 500, 0, 100);
    ASSERT_INT_EQ(cost_tracker_total_input(), 5000);
    ASSERT_INT_EQ(cost_tracker_total_output(), 2000);
    ASSERT_INT_EQ(cost_tracker_total_cache_read(), 3000);
    ASSERT_INT_EQ(cost_tracker_total_cache_write(), 500);
    ASSERT(cost_tracker_total_cost() > 0.0);
    char *f = cost_tracker_format();
    ASSERT_NOT_NULL(f);
    free(f);
    return 0;
}

static int test_cost_ext_cache_pricing(void) {
    cost_tracker_init();
    /* gpt-5.4: in=$10/M, out=$30/M, cache_read=$5/M (50%), cache_write=$10/M */
    cost_tracker_add_ext("gpt-5.4", 1000000, 0, 0, 0, 0, 0);
    double base_in = cost_tracker_total_cost();
    /* 1M input tokens at $10/M = $10 */
    ASSERT(fabs(base_in - 10.0) < 0.01);

    cost_tracker_init();
    cost_tracker_add_ext("gpt-5.4", 0, 0, 1000000, 0, 0, 0);
    double cache_rd = cost_tracker_total_cost();
    /* cache reads should be cheaper than full input */
    ASSERT(cache_rd > 0.0);
    ASSERT(cache_rd < base_in);

    cost_tracker_init();
    cost_tracker_add_ext("gpt-5.4", 0, 0, 0, 1000000, 0, 0);
    double cache_wr = cost_tracker_total_cost();
    ASSERT(cache_wr > 0.0);
    return 0;
}

static int test_tool_stats_init(void) {
    cost_tracker_init();
    ASSERT_INT_EQ(cost_tracker_tool_count(), 0);
    ASSERT_INT_EQ(cost_tracker_total_tool_calls(), 0);
    ASSERT_INT_EQ(cost_tracker_total_tool_fails(), 0);
    return 0;
}

static int test_tool_stats_add(void) {
    cost_tracker_init();
    cost_tracker_add_tool("file_read", 50, false);
    cost_tracker_add_tool("file_read", 30, false);
    cost_tracker_add_tool("bash", 1200, false);
    cost_tracker_add_tool("bash", 500, true);
    ASSERT_INT_EQ(cost_tracker_tool_count(), 2);
    ASSERT_INT_EQ(cost_tracker_total_tool_calls(), 4);
    ASSERT_INT_EQ(cost_tracker_total_tool_fails(), 1);
    return 0;
}

static int test_tool_stats_format(void) {
    cost_tracker_init();
    cost_tracker_add_tool("file_read", 100, false);
    cost_tracker_add_tool("grep", 200, false);
    char *f = cost_tracker_format();
    ASSERT_NOT_NULL(f);
    ASSERT(strstr(f, "Tool usage") != NULL);
    ASSERT(strstr(f, "file_read") != NULL);
    ASSERT(strstr(f, "grep") != NULL);
    free(f);
    return 0;
}

static int test_tool_stats_null(void) {
    cost_tracker_init();
    cost_tracker_add_tool(NULL, 0, false);
    ASSERT_INT_EQ(cost_tracker_tool_count(), 0);
    return 0;
}

static int test_model_overflow_safe(void) {
    cost_tracker_init();
    for (int i = 0; i < MAX_COST_MODELS + 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "model_%d", i);
        cost_tracker_add(name, 100, 50, 10);
    }
    ASSERT_INT_EQ(cost_tracker_total_input(), (MAX_COST_MODELS + 5) * 100);
    ASSERT_INT_EQ(cost_tracker_total_output(), (MAX_COST_MODELS + 5) * 50);
    ASSERT_INT_EQ(cost_tracker_total_requests(), MAX_COST_MODELS + 5);
    char *f = cost_tracker_format();
    ASSERT_NOT_NULL(f);
    free(f);
    return 0;
}

static int test_tool_stats_overflow_null(void) {
    cost_tracker_init();
    for (int i = 0; i < TOOL_STATS_MAX + 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "tool_%d", i);
        cost_tracker_add_tool(name, 10, false);
    }
    ASSERT_INT_EQ(cost_tracker_total_tool_calls(), TOOL_STATS_MAX + 3);
    ASSERT_INT_EQ(cost_tracker_total_tool_fails(), 0);
    char *f = cost_tracker_format();
    ASSERT_NOT_NULL(f);
    free(f);
    return 0;
}

int test_cost_tracker_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("cost_tracker");
    RUN_TEST(test_cost_init);
    RUN_TEST(test_cost_add_single);
    RUN_TEST(test_cost_add_multi_model);
    RUN_TEST(test_cost_format);
    RUN_TEST(test_cost_format_empty);
    RUN_TEST(test_cost_lines_tracking);
    RUN_TEST(test_cost_alt_provider_model);
    RUN_TEST(test_cost_accumulate);
    RUN_TEST(test_cost_ext_cache_tokens);
    RUN_TEST(test_cost_ext_cache_pricing);
    RUN_TEST(test_tool_stats_init);
    RUN_TEST(test_tool_stats_add);
    RUN_TEST(test_tool_stats_format);
    RUN_TEST(test_tool_stats_null);
    RUN_TEST(test_model_overflow_safe);
    RUN_TEST(test_tool_stats_overflow_null);
    TEST_RETURN_FAILED();
}
