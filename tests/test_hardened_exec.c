#include "test.h"
#include "nevercode.h"

static int test_echo(void) {
    char *o = NULL;
    hardened_result_t r;
    ASSERT_INT_EQ(hardened_exec_default("echo hi", &o, &r), 0);
    ASSERT_INT_EQ(r.exit_code, 0);
    ASSERT_STR_CONTAINS(o, "hi");
    free(o);
    return 0;
}

static int test_default_limits(void) {
    hardened_limits_t l;
    hardened_limits_default(&l);
    ASSERT_INT_EQ((int)l.cpu_time_limit_s, HARDENED_CPU_LIMIT_S);
    ASSERT_TRUE(l.mem_limit_bytes > 0);
    return 0;
}

static int test_format_stats(void) {
    char *s = hardened_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "hardened exec");
    free(s);
    return 0;
}

static int test_exit_code_nonzero(void) {
    char *o = NULL;
    hardened_result_t r;
    ASSERT_INT_EQ(hardened_exec_default("exit 42", &o, &r), 0);
    ASSERT_INT_EQ(r.exit_code, 42);
    free(o);
    return 0;
}

static int test_stderr_capture(void) {
    char *o = NULL;
    hardened_result_t r;
    ASSERT_INT_EQ(hardened_exec_default("echo ERR >&2", &o, &r), 0);
    ASSERT_INT_EQ(r.exit_code, 0);
    free(o);
    return 0;
}

static int test_quick_exec(void) {
    char *o = NULL;
    hardened_result_t r;
    ASSERT_INT_EQ(hardened_exec_quick("echo quick", 10, &o, &r), 0);
    ASSERT_INT_EQ(r.exit_code, 0);
    ASSERT_STR_CONTAINS(o, "quick");
    free(o);
    return 0;
}

int test_hardened_exec_all(void) {
    TEST_SUITE("hardened_exec");
    TEST_COUNTERS;
    RUN_TEST(test_echo);
    RUN_TEST(test_default_limits);
    RUN_TEST(test_format_stats);
    RUN_TEST(test_exit_code_nonzero);
    RUN_TEST(test_stderr_capture);
    RUN_TEST(test_quick_exec);
    TEST_RETURN_FAILED();
}
