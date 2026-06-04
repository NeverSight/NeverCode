#include "test.h"
#include "repl.h"

static int test_state_init(void) {
    stream_state_t s;
    stream_state_init(&s);
    ASSERT_NULL(s.text.data);
    ASSERT_INT_EQ(s.input_tokens, 0);
    ASSERT_INT_EQ(s.output_tokens, 0);
    ASSERT_INT_EQ(s.num_tc, 0);
    stream_state_free(&s);
    return 0;
}

static int test_state_free_safe(void) {
    stream_state_t s;
    stream_state_init(&s);
    stream_state_free(&s);
    stream_state_free(&s);
    return 0;
}

static int test_turn_num(void) {
    stream_state_t s;
    stream_state_init(&s);
    s.turn_num = 42;
    ASSERT_INT_EQ(s.turn_num, 42);
    stream_state_free(&s);
    return 0;
}

static int test_stop_reason(void) {
    stream_state_t s;
    stream_state_init(&s);
    s.stop_reason = STOP_END_TURN;
    ASSERT_INT_EQ(s.stop_reason, STOP_END_TURN);
    s.stop_reason = STOP_TOOL_USE;
    ASSERT_INT_EQ(s.stop_reason, STOP_TOOL_USE);
    s.stop_reason = STOP_MAX_TOKENS;
    ASSERT_INT_EQ(s.stop_reason, STOP_MAX_TOKENS);
    stream_state_free(&s);
    return 0;
}

int test_stream_handler_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("stream_handler");
    RUN_TEST(test_state_init);
    RUN_TEST(test_state_free_safe);
    RUN_TEST(test_turn_num);
    RUN_TEST(test_stop_reason);
    TEST_RETURN_FAILED();
}
