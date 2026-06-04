#include "test.h"
#include "nevercode.h"
#include "tools.h"

static int test_init(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);
    ASSERT_INT_EQ(streaming_exec_total(se), 0);
    ASSERT_INT_EQ(streaming_exec_completed(se), 0);
    ASSERT(streaming_exec_all_done(se));
    return 0;
}

static int test_tool_begin(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);
    streaming_exec_tool_begin(se, "tc_1", "Read");
    ASSERT_INT_EQ(streaming_exec_total(se), 1);
    ASSERT_STR_EQ(streaming_exec_tool_id(se, 0), "tc_1");
    ASSERT_STR_EQ(streaming_exec_tool_name(se, 0), "Read");
    ASSERT(!streaming_exec_tool_done(se, 0));
    streaming_exec_reset(se);
    return 0;
}

static int test_tool_delta(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);
    streaming_exec_tool_begin(se, "tc_2", "Bash");
    streaming_exec_tool_delta(se, "{\"comm");
    streaming_exec_tool_delta(se, "and\":\"ls\"}");
    ASSERT_STR_EQ(streaming_exec_tool_args(se, 0), "{\"command\":\"ls\"}");
    streaming_exec_reset(se);
    return 0;
}

static int test_tool_end(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);
    streaming_exec_tool_begin(se, "tc_3", "Glob");
    streaming_exec_tool_delta(se, "{\"pattern\":\"*.c\"}");
    streaming_exec_tool_end(se);
    ASSERT_INT_EQ(streaming_exec_ready_count(se), 1);
    streaming_exec_reset(se);
    return 0;
}

static int test_multiple_tools(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);
    streaming_exec_tool_begin(se, "tc_a", "Read");
    streaming_exec_tool_delta(se, "{\"path\":\"a.c\"}");
    streaming_exec_tool_end(se);
    streaming_exec_tool_begin(se, "tc_b", "Grep");
    streaming_exec_tool_delta(se, "{\"pattern\":\"foo\"}");
    streaming_exec_tool_end(se);
    ASSERT_INT_EQ(streaming_exec_total(se), 2);
    ASSERT_INT_EQ(streaming_exec_ready_count(se), 2);
    ASSERT(!streaming_exec_all_done(se));
    streaming_exec_reset(se);
    return 0;
}

static int test_out_of_bounds(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);
    ASSERT_NULL(streaming_exec_tool_id(se, 0));
    ASSERT_NULL(streaming_exec_tool_name(se, -1));
    ASSERT_NULL(streaming_exec_tool_output(se, 99));
    ASSERT_INT_EQ(streaming_exec_tool_rc(se, 0), -1);
    return 0;
}

static int test_null_handling(void) {
    streaming_exec_init(NULL);
    streaming_exec_tool_begin(NULL, "id", "name");
    streaming_exec_tool_delta(NULL, "frag");
    streaming_exec_tool_end(NULL);
    ASSERT_INT_EQ(streaming_exec_total(NULL), 0);
    ASSERT_INT_EQ(streaming_exec_ready_count(NULL), 0);
    ASSERT(streaming_exec_all_done(NULL));
    return 0;
}

static int test_reset(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);
    streaming_exec_tool_begin(se, "tc_x", "Read");
    streaming_exec_tool_delta(se, "{}");
    streaming_exec_tool_end(se);
    ASSERT_INT_EQ(streaming_exec_total(se), 1);
    streaming_exec_reset(se);
    ASSERT_INT_EQ(streaming_exec_total(se), 0);
    ASSERT(streaming_exec_all_done(se));
    return 0;
}

int test_streaming_exec_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("streaming_exec");
    RUN_TEST(test_init);
    RUN_TEST(test_tool_begin);
    RUN_TEST(test_tool_delta);
    RUN_TEST(test_tool_end);
    RUN_TEST(test_multiple_tools);
    RUN_TEST(test_out_of_bounds);
    RUN_TEST(test_null_handling);
    RUN_TEST(test_reset);
    TEST_RETURN_FAILED();
}
