#include "test.h"
#include "nevercode.h"

static int test_shell_init(void) {
    shell_session_init();
    ASSERT(!shell_session_active());
    shell_session_cleanup();
    return 0;
}

static int test_shell_echo(void) {
    shell_session_init();
    char *output = NULL;
    int rc = 0;
    int ret = shell_session_exec("echo hello_world", NULL, 5, &output, &rc);
    ASSERT(ret == 0);
    ASSERT(rc == 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "hello_world") != NULL);
    free(output);
    ASSERT(shell_session_active());
    shell_session_cleanup();
    return 0;
}

static int test_shell_env_persist(void) {
    shell_session_init();
    char *out1 = NULL, *out2 = NULL;
    int rc1 = 0, rc2 = 0;

    shell_session_exec("export NEVERCODE_TEST_VAR=42", NULL, 5, &out1, &rc1);
    free(out1);

    shell_session_exec("echo $NEVERCODE_TEST_VAR", NULL, 5, &out2, &rc2);
    ASSERT(rc2 == 0);
    ASSERT_NOT_NULL(out2);
    ASSERT(strstr(out2, "42") != NULL);
    free(out2);
    shell_session_cleanup();
    return 0;
}

static int test_shell_exit_code(void) {
    shell_session_init();
    char *output = NULL;
    int rc = 0;
    shell_session_exec("exit 7", NULL, 5, &output, &rc);
    /* Shell may die after exit, that's expected */
    free(output);
    shell_session_cleanup();
    return 0;
}

static int test_shell_reset(void) {
    shell_session_init();
    char *output = NULL;
    int rc = 0;
    shell_session_exec("echo test", NULL, 5, &output, &rc);
    free(output);
    ASSERT(shell_session_active());
    shell_session_reset();
    ASSERT(!shell_session_active());
    shell_session_cleanup();
    return 0;
}

static int test_shell_workdir(void) {
    shell_session_init();
    char *output = NULL;
    int rc = 0;
    shell_session_exec("pwd", "/tmp", 5, &output, &rc);
    ASSERT(rc == 0);
    ASSERT_NOT_NULL(output);
    /* /tmp might resolve to /private/tmp on macOS */
    ASSERT(strstr(output, "tmp") != NULL);
    free(output);
    shell_session_cleanup();
    return 0;
}

static int test_shell_multiline(void) {
    shell_session_init();
    char *output = NULL;
    int rc = 0;
    shell_session_exec("echo line1\necho line2\necho line3", NULL, 5, &output, &rc);
    ASSERT(rc == 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "line1") != NULL);
    ASSERT(strstr(output, "line3") != NULL);
    free(output);
    shell_session_cleanup();
    return 0;
}

int test_shell_session_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("shell_session");
    RUN_TEST(test_shell_init);
    RUN_TEST(test_shell_echo);
    RUN_TEST(test_shell_env_persist);
    RUN_TEST(test_shell_exit_code);
    RUN_TEST(test_shell_reset);
    RUN_TEST(test_shell_workdir);
    RUN_TEST(test_shell_multiline);
    TEST_RETURN_FAILED();
}
