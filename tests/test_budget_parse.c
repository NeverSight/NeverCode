/*
 * test_budget_parse.c — Tests for token budget shorthand parsing.
 */
#include "test.h"
#include "nevercode.h"

static int t_null_and_empty(void) {
    ASSERT_INT_EQ((int)budget_parse_shorthand(NULL), 0);
    ASSERT_INT_EQ((int)budget_parse_shorthand(""), 0);
    ASSERT_INT_EQ((int)budget_parse_shorthand("hello world"), 0);
    return 0;
}

static int t_shorthand_start(void) {
    ASSERT_INT_EQ((int)budget_parse_shorthand("+500k"), 500000);
    ASSERT_INT_EQ((int)budget_parse_shorthand("+2M"), 2000000);
    ASSERT_INT_EQ((int)budget_parse_shorthand("+1.5M"), 1500000);
    ASSERT_INT_EQ((int)budget_parse_shorthand("+1B"), 1000000000);
    return 0;
}

static int t_shorthand_end(void) {
    long v = budget_parse_shorthand("fix the bug +500k");
    ASSERT(v == 500000);
    return 0;
}

static int t_verbose_use(void) {
    long v1 = budget_parse_shorthand("use 2M tokens");
    ASSERT(v1 == 2000000);
    long v2 = budget_parse_shorthand("please spend 500k tokens on this");
    ASSERT(v2 == 500000);
    return 0;
}

static int t_no_match(void) {
    ASSERT_INT_EQ((int)budget_parse_shorthand("just do it"), 0);
    ASSERT_INT_EQ((int)budget_parse_shorthand("+"), 0);
    ASSERT_INT_EQ((int)budget_parse_shorthand("+abc"), 0);
    return 0;
}

static int t_macros(void) {
    ASSERT(BUDGET_SHORTHAND_MAX_VALUE > 0);
    return 0;
}

int test_budget_parse_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("budget_parse");
    RUN_TEST(t_null_and_empty);
    RUN_TEST(t_shorthand_start);
    RUN_TEST(t_shorthand_end);
    RUN_TEST(t_verbose_use);
    RUN_TEST(t_no_match);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
