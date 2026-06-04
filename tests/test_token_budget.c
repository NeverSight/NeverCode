#include "test.h"
#include "nevercode.h"

static int test_init_disabled(void) {
    token_budget_init(0);
    ASSERT(token_budget_active() == false);
    ASSERT_INT_EQ(token_budget_get(), 0);
    return 0;
}

static int test_init_enabled(void) {
    token_budget_init(50000);
    ASSERT(token_budget_active() == true);
    ASSERT_INT_EQ(token_budget_get(), 50000);
    return 0;
}

static int test_set_budget(void) {
    token_budget_init(0);
    ASSERT(token_budget_active() == false);
    token_budget_set(100000);
    ASSERT(token_budget_active() == true);
    ASSERT_INT_EQ(token_budget_get(), 100000);
    return 0;
}

static int test_start_turn(void) {
    token_budget_init(50000);
    token_budget_start_turn();
    ASSERT_INT_EQ(token_budget_continuation_count(), 0);
    return 0;
}

static int test_continuation_msg(void) {
    char *msg = token_budget_continuation_msg(1, 3);
    ASSERT_NOT_NULL(msg);
    ASSERT(strstr(msg, "truncated") != NULL);
    ASSERT(strstr(msg, "1/3") != NULL);
    free(msg);
    return 0;
}

static int test_format_disabled(void) {
    token_budget_init(0);
    char *s = token_budget_format(500);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "disabled") != NULL);
    free(s);
    return 0;
}

static int test_format_enabled(void) {
    token_budget_init(100000);
    token_budget_start_turn();
    char *s = token_budget_format(25000);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "25k") != NULL || strstr(s, "100k") != NULL);
    free(s);
    return 0;
}

static int test_elapsed(void) {
    token_budget_init(50000);
    token_budget_start_turn();
    long e = token_budget_elapsed_s();
    ASSERT(e >= 0);
    ASSERT(e < 5);
    return 0;
}

int test_token_budget_all(void) {
    TEST_SUITE("token_budget");
    TEST_COUNTERS;

    RUN_TEST(test_init_disabled);
    RUN_TEST(test_init_enabled);
    RUN_TEST(test_set_budget);
    RUN_TEST(test_start_turn);
    RUN_TEST(test_continuation_msg);
    RUN_TEST(test_format_disabled);
    RUN_TEST(test_format_enabled);
    RUN_TEST(test_elapsed);

    TEST_RETURN_FAILED();
}
