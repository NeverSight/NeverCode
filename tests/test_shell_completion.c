/*
 * test_shell_completion.c — Tests for shell completion generation
 */

#include "test.h"
#include "nevercode.h"

static int test_gen_zsh(void) {
    char *s = shell_completion_generate("zsh");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "#compdef nevercode");
    ASSERT_STR_CONTAINS(s, "--model");
    ASSERT_STR_CONTAINS(s, "--help");
    free(s);
    return 0;
}

static int test_gen_bash(void) {
    char *s = shell_completion_generate("bash");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "complete -F");
    ASSERT_STR_CONTAINS(s, "--provider");
    free(s);
    return 0;
}

static int test_gen_fish(void) {
    char *s = shell_completion_generate("fish");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "complete -c nevercode");
    ASSERT_STR_CONTAINS(s, "model");
    free(s);
    return 0;
}

static int test_gen_unknown(void) {
    char *s = shell_completion_generate("powershell");
    ASSERT_NULL(s);
    return 0;
}

static int test_detect_returns_string(void) {
    const char *d = shell_completion_detect();
    ASSERT_NOT_NULL(d);
    ASSERT_TRUE(strlen(d) > 0);
    return 0;
}

int test_shell_completion_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("shell_completion");
    RUN_TEST(test_gen_zsh);
    RUN_TEST(test_gen_bash);
    RUN_TEST(test_gen_fish);
    RUN_TEST(test_gen_unknown);
    RUN_TEST(test_detect_returns_string);
    return _t_failed;
}
