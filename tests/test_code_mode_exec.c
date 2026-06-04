#include "test.h"
#include "nevercode.h"
static int t1(void) {
    code_mode_exec_reset();
    ASSERT(code_mode_exec_status() == CM_EXEC_IDLE);
    return 0;
}
static int t2(void) {
    code_mode_exec_reset();
    ASSERT_INT_EQ(code_mode_exec_start("c1", "print('hi')"), 0);
    ASSERT(code_mode_exec_status() == CM_EXEC_RUNNING);
    code_mode_exec_reset();
    return 0;
}
static int t3(void) {
    code_mode_exec_reset();
    code_mode_exec_start("c2", "x");
    ASSERT_INT_EQ(code_mode_exec_yield("partial"), 0);
    ASSERT(code_mode_exec_status() == CM_EXEC_YIELDED);
    ASSERT_INT_EQ(code_mode_exec_resume(), 0);
    ASSERT(code_mode_exec_status() == CM_EXEC_RUNNING);
    code_mode_exec_reset();
    return 0;
}
static int t4(void) {
    code_mode_exec_reset();
    code_mode_exec_start("c3", "s");
    code_mode_exec_complete("final", NULL);
    ASSERT(code_mode_exec_status() == CM_EXEC_COMPLETED);
    ASSERT_STR_EQ(code_mode_exec_output(), "final");
    code_mode_exec_reset();
    return 0;
}
static int t5(void) {
    code_mode_exec_reset();
    code_mode_exec_start("c4", "s");
    code_mode_exec_complete(NULL, "SyntaxError");
    ASSERT(code_mode_exec_status() == CM_EXEC_FAILED);
    code_mode_exec_reset();
    return 0;
}
static int t6(void) {
    code_mode_exec_reset();
    code_mode_exec_start("c5", "s");
    code_mode_exec_notify("L1\n");
    code_mode_exec_notify("L2\n");
    ASSERT_STR_CONTAINS(code_mode_exec_output(), "L1");
    code_mode_exec_reset();
    return 0;
}
static int t7(void) {
    code_mode_exec_reset();
    code_mode_exec_start("c6", "x=1");
    code_mode_exec_complete("r:1", NULL);
    char *f = code_mode_exec_format_result();
    ASSERT_STR_CONTAINS(f, "completed");
    free(f);
    code_mode_exec_reset();
    return 0;
}
static int t8(void) {
    code_mode_exec_reset();
    code_mode_exec_start("c7", "s");
    code_mode_exec_record_tool_call();
    code_mode_exec_record_tool_call();
    code_mode_exec_complete("d", NULL);
    char *f = code_mode_exec_format_result();
    ASSERT_STR_CONTAINS(f, "tool calls: 2");
    free(f);
    code_mode_exec_reset();
    return 0;
}
static int t9(void) {
    code_mode_exec_reset();
    ASSERT(code_mode_exec_start("c8", NULL) < 0);
    return 0;
}
static int t10(void) {
    code_mode_exec_reset();
    code_mode_exec_start("c9", "s");
    ASSERT(code_mode_exec_wall_ms() >= 0);
    code_mode_exec_complete("ok", NULL);
    code_mode_exec_reset();
    return 0;
}
int test_code_mode_exec_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("code_mode_exec");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(t10);
    TEST_RETURN_FAILED();
}
