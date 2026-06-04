#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

/* Uses declarations from nevercode.h */

static int test_init(void) {
    tool_call_rt_init();
    ASSERT_INT_EQ(0, tool_call_rt_count());
    ASSERT_INT_EQ(0, tool_call_rt_pending_count());
    ASSERT_INT_EQ(0, tool_call_rt_completed_total());
    tool_call_rt_free();
    return 0;
}

static int test_enqueue(void) {
    tool_call_rt_init();
    int idx = tool_call_rt_enqueue("call-1", "read", NULL);
    ASSERT_GE(idx, 0);
    ASSERT_INT_EQ(1, tool_call_rt_count());
    ASSERT_INT_EQ(1, tool_call_rt_pending_count());
    tool_call_rt_free();
    return 0;
}

static int test_cancel(void) {
    tool_call_rt_init();
    int idx = tool_call_rt_enqueue("call-1", "read", NULL);
    int rc = tool_call_rt_cancel(idx);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(0, tool_call_rt_pending_count());
    tool_call_rt_free();
    return 0;
}

static int test_reset(void) {
    tool_call_rt_init();
    tool_call_rt_enqueue("a", "tool_a", NULL);
    tool_call_rt_enqueue("b", "tool_b", NULL);
    ASSERT_INT_EQ(2, tool_call_rt_count());
    tool_call_rt_reset();
    ASSERT_INT_EQ(0, tool_call_rt_count());
    tool_call_rt_free();
    return 0;
}

static int test_format(void) {
    tool_call_rt_init();
    tool_call_rt_enqueue("c1", "bash", NULL);
    char *fmt = tool_call_rt_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "ToolCallRT");
    ASSERT_STR_CONTAINS(fmt, "bash");
    free(fmt);
    tool_call_rt_free();
    return 0;
}

int test_tool_call_rt_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_call_rt");
    RUN_TEST(test_init);
    RUN_TEST(test_enqueue);
    RUN_TEST(test_cancel);
    RUN_TEST(test_reset);
    RUN_TEST(test_format);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("tool_call_rt");
    RUN_TEST(test_init);
    RUN_TEST(test_enqueue);
    RUN_TEST(test_cancel);
    RUN_TEST(test_reset);
    RUN_TEST(test_format);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
