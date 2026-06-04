#include "test.h"
#include "nevercode.h"

static int test_init(void) {
    int e = proc_harden_init();
    ASSERT_GE(e, 0);
    return 0;
}

static int test_core_dumps(void) {
    proc_harden_init();
    (void)proc_harden_core_dumps_off();
    return 0;
}

static int test_ptrace(void) {
    proc_harden_init();
    (void)proc_harden_ptrace_denied();
    return 0;
}

static int test_env_cleared(void) {
    proc_harden_init();
    ASSERT_GE(proc_harden_env_cleared(), 0);
    return 0;
}

static int test_format(void) {
    proc_harden_init();
    char *s = proc_harden_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Process Hardening");
    ASSERT_STR_CONTAINS(s, "Core dumps");
    free(s);
    return 0;
}

int test_proc_harden_all(void) {
    TEST_SUITE("proc_harden");
    TEST_COUNTERS;
    RUN_TEST(test_init);
    RUN_TEST(test_core_dumps);
    RUN_TEST(test_ptrace);
    RUN_TEST(test_env_cleared);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
