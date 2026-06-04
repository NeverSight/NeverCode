/*
 * test_cache_strategy.c — Tests for prompt cache breakpoint strategy.
 */
#include "test.h"
#include "nevercode.h"

static int t_system_and_n_empty(void) {
    ASSERT_INT_EQ(cache_strategy_system_and_n(0, 4), 0);
    return 0;
}

static int t_system_and_n_one(void) {
    ASSERT_INT_EQ(cache_strategy_system_and_n(1, 4), 1);
    return 0;
}

static int t_system_and_n_four(void) {
    ASSERT_INT_EQ(cache_strategy_system_and_n(5, 4), 4);
    return 0;
}

static int t_system_and_n_capped(void) {
    ASSERT_INT_EQ(cache_strategy_system_and_n(100, 4), 4);
    return 0;
}

static int t_mark_null(void) {
    ASSERT_INT_EQ(cache_strategy_mark(NULL, 0, 4), 0);
    return 0;
}

static int t_macros(void) {
    ASSERT(CACHE_STRATEGY_MAX_BREAKPOINTS > 0);
    ASSERT(CACHE_STRATEGY_DEFAULT_TAIL > 0);
    return 0;
}

int test_cache_strategy_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("cache_strategy");
    RUN_TEST(t_system_and_n_empty);
    RUN_TEST(t_system_and_n_one);
    RUN_TEST(t_system_and_n_four);
    RUN_TEST(t_system_and_n_capped);
    RUN_TEST(t_mark_null);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
