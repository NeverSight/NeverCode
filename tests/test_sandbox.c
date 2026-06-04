/*
 * test_sandbox.c — Tests for sandbox path enforcement (opt-in).
 *
 * Default (not enforcing): all paths allowed.
 * Enforcing: only project root, /tmp, /dev, and home config dirs allowed.
 */
#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <unistd.h>

/* ── Default mode (enforcement off): everything allowed ── */

static int t_default_allow_all(void) {
    sandbox_init("/tmp/test_sandbox_proj");
    sandbox_disable();
    ASSERT_TRUE(sandbox_check_path("src/main.c"));
    ASSERT_TRUE(sandbox_check_path("/etc/passwd"));
    ASSERT_TRUE(sandbox_check_path("/var/log/syslog"));
    ASSERT_TRUE(sandbox_check_path(NULL));
    ASSERT_TRUE(sandbox_check_path(""));
    return 0;
}

/* ── Enforcing mode: project-scoped paths ── */

static int t_enforce_allow_project(void) {
    sandbox_init("/tmp/test_sandbox_proj");
    sandbox_enable();
    ASSERT_TRUE(sandbox_check_path("/tmp/test_sandbox_proj/src/main.c"));
    ASSERT_TRUE(sandbox_check_path("/tmp/test_sandbox_proj/README.md"));
    sandbox_disable();
    return 0;
}

static int t_enforce_allow_relative(void) {
    sandbox_init("/tmp/test_sandbox_proj");
    sandbox_enable();
    ASSERT_TRUE(sandbox_check_path("src/main.c"));
    ASSERT_TRUE(sandbox_check_path("README.md"));
    ASSERT_TRUE(sandbox_check_path(""));
    sandbox_disable();
    return 0;
}

static int t_enforce_allow_tmp(void) {
    sandbox_init("/tmp/test_sandbox_proj");
    sandbox_enable();
    ASSERT_TRUE(sandbox_check_path("/tmp/foo.txt"));
    ASSERT_TRUE(sandbox_check_path("/tmp/nevercode-results/output.txt"));
    sandbox_disable();
    return 0;
}

static int t_enforce_allow_dev(void) {
    sandbox_init("/tmp/test_sandbox_proj");
    sandbox_enable();
    ASSERT_TRUE(sandbox_check_path("/dev/null"));
    ASSERT_TRUE(sandbox_check_path("/dev/stdin"));
    sandbox_disable();
    return 0;
}

static int t_enforce_allow_home_config(void) {
    const char *home = getenv(ENV_HOME);
    if (!home)
        return 0;
    sandbox_init("/tmp/test_sandbox_proj");
    sandbox_enable();
    char path[1024];
    snprintf(path, sizeof(path), "%s/.nevercode/config.json", home);
    ASSERT_TRUE(sandbox_check_path(path));
    snprintf(path, sizeof(path), "%s/.claude/settings.json", home);
    ASSERT_TRUE(sandbox_check_path(path));
    snprintf(path, sizeof(path), "%s/.agents/config.json", home);
    ASSERT_TRUE(sandbox_check_path(path));
    sandbox_disable();
    return 0;
}

static int t_enforce_deny_outside(void) {
    sandbox_init("/tmp/test_sandbox_proj");
    sandbox_enable();
    ASSERT_FALSE(sandbox_check_path("/etc/passwd"));
    ASSERT_FALSE(sandbox_check_path("/var/log/syslog"));
    ASSERT_FALSE(sandbox_check_path("/usr/bin/ls"));
    /* Path traversal via ".." must be caught */
    ASSERT_FALSE(sandbox_check_path("/tmp/test_sandbox_proj/../../etc/passwd"));
    sandbox_disable();
    return 0;
}

static int t_enforce_protect_git(void) {
    sandbox_init("/tmp/test_sandbox_proj");
    sandbox_enable();
    /* .git/ and .nevercode/ inside project root must be write-denied */
    ASSERT_FALSE(sandbox_check_path("/tmp/test_sandbox_proj/.git/hooks/pre-commit"));
    ASSERT_FALSE(sandbox_check_path("/tmp/test_sandbox_proj/.git/config"));
    ASSERT_FALSE(sandbox_check_path("/tmp/test_sandbox_proj/.nevercode/config.json"));
    /* Normal project files are still allowed */
    ASSERT_TRUE(sandbox_check_path("/tmp/test_sandbox_proj/src/main.c"));
    sandbox_disable();
    return 0;
}

static int t_enforce_null_empty(void) {
    sandbox_init("/tmp/test_sandbox_proj");
    sandbox_enable();
    ASSERT_TRUE(sandbox_check_path(NULL));
    ASSERT_TRUE(sandbox_check_path(""));
    sandbox_disable();
    return 0;
}

static int t_enforcing_flag(void) {
    ASSERT_FALSE(sandbox_enforcing());
    sandbox_enable();
    ASSERT_TRUE(sandbox_enforcing());
    sandbox_disable();
    ASSERT_FALSE(sandbox_enforcing());
    return 0;
}

int test_sandbox_all(void) {
    TEST_SUITE("sandbox");
    TEST_COUNTERS;
    RUN_TEST(t_default_allow_all);
    RUN_TEST(t_enforce_allow_project);
    RUN_TEST(t_enforce_allow_relative);
    RUN_TEST(t_enforce_allow_tmp);
    RUN_TEST(t_enforce_allow_dev);
    RUN_TEST(t_enforce_allow_home_config);
    RUN_TEST(t_enforce_deny_outside);
    RUN_TEST(t_enforce_protect_git);
    RUN_TEST(t_enforce_null_empty);
    RUN_TEST(t_enforcing_flag);
    TEST_RETURN_FAILED();
}
