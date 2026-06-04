/*
 * test_path_sandbox.c — Path sandbox init and root tracking tests.
 */

#include "test.h"
#include "nevercode.h"
#include <unistd.h>

static int test_sandbox_init(void) {
    sandbox_init("/tmp/test_sandbox_root");
    ASSERT_NOT_NULL(sandbox_root());
    ASSERT_STR_EQ(sandbox_root(), "/tmp/test_sandbox_root");
    return 0;
}

static int test_sandbox_allow_within(void) {
    sandbox_init("/tmp/test_sandbox_root");
    ASSERT(sandbox_check_path("/tmp/test_sandbox_root/foo.c") == true);
    ASSERT(sandbox_check_path("/tmp/test_sandbox_root/sub/dir/bar.c") == true);
    return 0;
}

static int test_default_no_enforcement(void) {
    sandbox_init("/tmp/test_sandbox_root");
    sandbox_disable();
    /* Without enforcement, all paths are allowed */
    ASSERT(sandbox_check_path("/etc/passwd") == true);
    ASSERT(sandbox_check_path("/var/log/syslog") == true);
    return 0;
}

static int test_enforce_deny_outside(void) {
    sandbox_init("/tmp/test_sandbox_root");
    sandbox_enable();
    ASSERT(sandbox_check_path("/etc/passwd") == false);
    ASSERT(sandbox_check_path("/var/log/syslog") == false);
    /* Traversal via ".." must be blocked */
    ASSERT(sandbox_check_path("/tmp/test_sandbox_root/../../etc/passwd") == false);
    sandbox_disable();
    return 0;
}

static int test_sandbox_allow_home_config(void) {
    sandbox_init("/tmp/test_sandbox_root");
    const char *home = getenv(ENV_HOME);
    if (home) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/.nevercode/config.json", home);
        ASSERT(sandbox_check_path(path) == true);
        snprintf(path, sizeof(path), "%s/.claude/settings.json", home);
        ASSERT(sandbox_check_path(path) == true);
    }
    return 0;
}

static int test_sandbox_allow_tmp(void) {
    sandbox_init("/tmp/test_sandbox_root");
    ASSERT(sandbox_check_path("/tmp/somefile") == true);
    ASSERT(sandbox_check_path("/tmp/nevercode-results/output.txt") == true);
    return 0;
}

static int test_sandbox_allow_relative(void) {
    sandbox_init("/tmp/test_sandbox_root");
    ASSERT(sandbox_check_path("foo.c") == true);
    ASSERT(sandbox_check_path("src/main.c") == true);
    return 0;
}

static int test_sandbox_null_empty(void) {
    sandbox_init("/tmp/test_sandbox_root");
    ASSERT(sandbox_check_path(NULL) == true);
    ASSERT(sandbox_check_path("") == true);
    return 0;
}

static int test_sandbox_allow_dev(void) {
    sandbox_init("/tmp/test_sandbox_root");
    ASSERT(sandbox_check_path("/dev/null") == true);
    ASSERT(sandbox_check_path("/dev/stdin") == true);
    return 0;
}

int test_path_sandbox_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("path_sandbox");
    RUN_TEST(test_sandbox_init);
    RUN_TEST(test_sandbox_allow_within);
    RUN_TEST(test_default_no_enforcement);
    RUN_TEST(test_enforce_deny_outside);
    RUN_TEST(test_sandbox_allow_home_config);
    RUN_TEST(test_sandbox_allow_tmp);
    RUN_TEST(test_sandbox_allow_relative);
    RUN_TEST(test_sandbox_null_empty);
    RUN_TEST(test_sandbox_allow_dev);
    TEST_RETURN_FAILED();
}
