#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

/* Uses declarations from nevercode.h */

static int test_init_clean(void) {
    abort_handler_init();
    ASSERT_INT_EQ(0, abort_pending_count());
    ASSERT_INT_EQ(0, abort_total_count());
    ASSERT_INT_EQ(0, abort_forced_count());
    ASSERT_FALSE(abort_is_global());
    abort_handler_free();
    return 0;
}

static int test_request_basic(void) {
    abort_handler_init();
    int rc = abort_request("task-1", ABORT_USER_REQ);
    ASSERT_INT_EQ(0, rc);
    ASSERT_TRUE(abort_is_pending("task-1"));
    ASSERT_INT_EQ(1, abort_pending_count());
    abort_handler_free();
    return 0;
}

static int test_request_duplicate(void) {
    abort_handler_init();
    abort_request("task-1", ABORT_USER_REQ);
    int rc = abort_request("task-1", ABORT_USER_REQ);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, abort_pending_count());
    abort_handler_free();
    return 0;
}

static int test_not_pending(void) {
    abort_handler_init();
    ASSERT_FALSE(abort_is_pending("nonexistent"));
    abort_handler_free();
    return 0;
}

static int test_global_abort(void) {
    abort_handler_init();
    ASSERT_FALSE(abort_is_global());
    abort_global(ABORT_SIGNAL_REQ);
    ASSERT_TRUE(abort_is_global());
    abort_handler_free();
    return 0;
}

static int test_format(void) {
    abort_handler_init();
    abort_request("task-a", ABORT_USER_REQ);
    char *fmt = abort_format_status();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "task-a");
    ASSERT_STR_CONTAINS(fmt, "pending=1");
    free(fmt);
    abort_handler_free();
    return 0;
}

int test_abort_handler_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("abort_handler");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_request_basic);
    RUN_TEST(test_request_duplicate);
    RUN_TEST(test_not_pending);
    RUN_TEST(test_global_abort);
    RUN_TEST(test_format);
    return _t_failed;
}

#if 0 // disabled for unified build
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("abort_handler");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_request_basic);
    RUN_TEST(test_request_duplicate);
    RUN_TEST(test_not_pending);
    RUN_TEST(test_global_abort);
    RUN_TEST(test_format);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
