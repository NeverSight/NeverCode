#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init_and_state(void) {
    cscope_t cs;
    cscope_init(&cs, "run-1", NULL);
    ASSERT_INT_EQ(cs.state, CSCOPE_ACTIVE);
    ASSERT_INT_EQ(cscope_depth(&cs), 0);
    ASSERT_FALSE(cscope_is_cancelled(&cs));
    ASSERT_FALSE(cscope_should_stop(&cs));
    ASSERT_STR_EQ(cscope_state_name(CSCOPE_ACTIVE), "active");
    return 0;
}

static int t_cancel_request(void) {
    cscope_t cs;
    cscope_init(&cs, "run-2", NULL);
    cscope_request_cancel(&cs);
    ASSERT_TRUE(cscope_is_cancelled(&cs));
    ASSERT_TRUE(cscope_should_stop(&cs));
    ASSERT_STR_EQ(cscope_state_name(cs.state), "cancel_requested");
    return 0;
}

static int t_parent_propagation(void) {
    cscope_t parent, child;
    cscope_init(&parent, "p", NULL);
    cscope_init(&child, "c", &parent);
    ASSERT_INT_EQ(cscope_depth(&child), 1);
    ASSERT_FALSE(cscope_is_cancelled(&child));

    cscope_request_cancel(&parent);
    ASSERT_TRUE(cscope_is_cancelled(&child));
    ASSERT_TRUE(cscope_should_stop(&child));
    return 0;
}

static int t_reset(void) {
    cscope_t cs;
    cscope_init(&cs, "r", NULL);
    cscope_request_cancel(&cs);
    ASSERT_TRUE(cscope_is_cancelled(&cs));
    cscope_reset(&cs);
    ASSERT_FALSE(cscope_is_cancelled(&cs));
    ASSERT_INT_EQ(cs.state, CSCOPE_ACTIVE);
    return 0;
}

static int t_null_safety(void) {
    ASSERT_FALSE(cscope_is_cancelled(NULL));
    ASSERT_FALSE(cscope_should_stop(NULL));
    ASSERT_INT_EQ(cscope_depth(NULL), -1);
    cscope_request_cancel(NULL);
    cscope_reset(NULL);
    return 0;
}

static int t_state_names(void) {
    ASSERT_STR_EQ(cscope_state_name(CSCOPE_ACTIVE), "active");
    ASSERT_STR_EQ(cscope_state_name(CSCOPE_CANCEL_REQUESTED), "cancel_requested");
    ASSERT_STR_EQ(cscope_state_name(CSCOPE_CANCELLED), "cancelled");
    return 0;
}

int test_cancel_scope_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("cancel_scope");
    RUN_TEST(t_init_and_state);
    RUN_TEST(t_cancel_request);
    RUN_TEST(t_parent_propagation);
    RUN_TEST(t_reset);
    RUN_TEST(t_null_safety);
    RUN_TEST(t_state_names);
    TEST_RETURN_FAILED();
}
