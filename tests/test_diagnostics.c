#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

/* Uses declarations from nevercode.h */

static int test_init_clean(void) {
    diagnostics_init();
    ASSERT_INT_EQ(0, diagnostics_total());
    ASSERT_INT_EQ(0, diagnostics_pass_count());
    ASSERT_INT_EQ(0, diagnostics_warn_count());
    ASSERT_INT_EQ(0, diagnostics_fail_count());
    sys_diagnostics_free();
    return 0;
}

static int test_format_empty(void) {
    diagnostics_init();
    char *fmt = diagnostics_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "0 checks");
    free(fmt);
    sys_diagnostics_free();
    return 0;
}

static int test_run_returns_nonzero(void) {
    diagnostics_init();
    int n = diagnostics_run();
    ASSERT_GT(n, 0);
    ASSERT_INT_EQ(n, diagnostics_total());
    ASSERT_GE(diagnostics_elapsed_ms(), 0);
    int p = diagnostics_pass_count();
    int w = diagnostics_warn_count();
    int f = diagnostics_fail_count();
    ASSERT_INT_EQ(n, p + w + f);
    sys_diagnostics_free();
    return 0;
}

static int test_exec_produces_output(void) {
    char *output = NULL;
    int rc = diagnostics_exec(NULL, &output, NULL);
    ASSERT_INT_EQ(0, rc);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Diagnostics");
    free(output);
    sys_diagnostics_free();
    return 0;
}

static int test_double_run(void) {
    diagnostics_init();
    diagnostics_run();
    int first = diagnostics_total();
    diagnostics_run();
    int second = diagnostics_total();
    ASSERT_INT_EQ(first, second);
    sys_diagnostics_free();
    return 0;
}

int test_diagnostics_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("diagnostics");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_run_returns_nonzero);
    RUN_TEST(test_exec_produces_output);
    RUN_TEST(test_double_run);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("diagnostics");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_run_returns_nonzero);
    RUN_TEST(test_exec_produces_output);
    RUN_TEST(test_double_run);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
