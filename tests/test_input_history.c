#include "test.h"
#include "nevercode.h"

static int t_init(void) {
    input_history_init();
    ASSERT_INT_EQ(input_history_count(), 0);
    ASSERT_NULL(input_history_up());
    input_history_free();
    return 0;
}

static int t_push_and_up(void) {
    input_history_init();
    input_history_push("first");
    input_history_push("second");
    input_history_push("third");
    ASSERT_INT_EQ(input_history_count(), 3);

    ASSERT_STR_EQ(input_history_up(), "third");
    ASSERT_STR_EQ(input_history_up(), "second");
    ASSERT_STR_EQ(input_history_up(), "first");
    ASSERT_STR_EQ(input_history_up(), "first");
    input_history_free();
    return 0;
}

static int t_down(void) {
    input_history_init();
    input_history_push("a");
    input_history_push("b");

    input_history_up();
    input_history_up();
    ASSERT_STR_EQ(input_history_down(), "b");
    ASSERT_NULL(input_history_down());
    input_history_free();
    return 0;
}

static int t_dedup(void) {
    input_history_init();
    input_history_push("same");
    input_history_push("same");
    ASSERT_INT_EQ(input_history_count(), 1);
    input_history_free();
    return 0;
}

static int t_trim(void) {
    input_history_init();
    input_history_push("  hello  \n");
    ASSERT_INT_EQ(input_history_count(), 1);
    ASSERT_STR_EQ(input_history_up(), "hello");
    input_history_free();
    return 0;
}

static int t_empty_skip(void) {
    input_history_init();
    input_history_push("");
    input_history_push("   ");
    input_history_push("\n");
    ASSERT_INT_EQ(input_history_count(), 0);
    input_history_free();
    return 0;
}

static int t_reset(void) {
    input_history_init();
    input_history_push("x");
    input_history_up();
    input_history_reset_cursor();
    ASSERT_NULL(input_history_down());
    input_history_free();
    return 0;
}

int test_input_history_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("input_history");
    RUN_TEST(t_init);
    RUN_TEST(t_push_and_up);
    RUN_TEST(t_down);
    RUN_TEST(t_dedup);
    RUN_TEST(t_trim);
    RUN_TEST(t_empty_skip);
    RUN_TEST(t_reset);
    TEST_RETURN_FAILED();
}
