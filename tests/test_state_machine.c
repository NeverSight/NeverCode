/*
 * test_state_machine.c
 */

#include "test.h"
#include "nevercode.h"
#include <string.h>

static int saw = 0;
static session_phase_t last_from, last_to;

static void cb(session_phase_t from, session_phase_t to, void *ctx) {
    (void)ctx;
    saw++;
    last_from = from;
    last_to = to;
}

static int test_lifecycle(void) {
    state_machine_init("t1");
    ASSERT_STR_EQ(state_session_id(), "t1");
    ASSERT_INT_EQ((int)state_session_phase(), (int)SESSION_IDLE);
    ASSERT_INT_EQ(state_session_begin(), 0);
    ASSERT_INT_EQ((int)state_session_phase(), (int)SESSION_INITIALIZING);
    ASSERT_INT_EQ(state_session_ready(), 0);
    ASSERT_INT_EQ(state_session_run(), 0);
    ASSERT_INT_EQ(state_session_compact(), 0);
    ASSERT_INT_EQ(state_session_run(), 0);
    ASSERT_INT_EQ(state_session_suspend(), 0);
    ASSERT_INT_EQ(state_session_resume(), 0);
    ASSERT_INT_EQ(state_session_shutdown(), 0);
    ASSERT_INT_EQ(state_session_terminate(), 0);
    ASSERT_INT_EQ((int)state_session_phase(), (int)SESSION_TERMINATED);
    state_machine_free();
    return 0;
}

static int test_invalid_transition(void) {
    state_machine_init("t2");
    ASSERT_INT_EQ(state_session_ready(), -1);
    ASSERT_INT_EQ(state_session_begin(), 0);
    ASSERT_INT_EQ(state_session_run(), -1);
    ASSERT_INT_EQ(state_session_ready(), 0);
    state_machine_free();
    return 0;
}

static int test_turn_flow(void) {
    state_machine_init("t3");
    state_session_begin();
    state_session_ready();
    state_session_run();
    ASSERT_INT_EQ(state_turn_begin(), 0);
    ASSERT_INT_EQ(state_turn_api_call(), 0);
    ASSERT_INT_EQ(state_turn_streaming(), 0);
    ASSERT_INT_EQ(state_turn_tool_route(), 0);
    ASSERT_INT_EQ(state_turn_tool_exec(), 0);
    ASSERT_INT_EQ(state_turn_result_collect(), 0);
    ASSERT_INT_EQ(state_turn_post_process(), 0);
    ASSERT_INT_EQ(state_turn_complete(), 0);
    ASSERT_INT_EQ(state_turn_tool_count(), 1);
    state_machine_free();
    return 0;
}

static int test_callback(void) {
    saw = 0;
    state_machine_init("t4");
    ASSERT(state_add_transition_cb(cb, NULL) >= 0);
    state_session_begin();
    ASSERT_INT_EQ(saw, 1);
    ASSERT_INT_EQ((int)last_from, (int)SESSION_IDLE);
    ASSERT_INT_EQ((int)last_to, (int)SESSION_INITIALIZING);
    state_machine_free();
    return 0;
}

static int test_format(void) {
    state_machine_init("t5");
    char *s = state_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "t5") != NULL);
    free(s);
    state_machine_free();
    return 0;
}

int test_state_machine_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("state_machine");
    RUN_TEST(test_lifecycle);
    RUN_TEST(test_invalid_transition);
    RUN_TEST(test_turn_flow);
    RUN_TEST(test_callback);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
