#include "test.h"
#include "nevercode.h"
#include <signal.h>
static int t1(void) {
    char *o = NULL;
    int ec = -1;
    int rc = fork_exec_simple("echo hello", NULL, 5000, &o, &ec);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(ec, 0);
    ASSERT_NOT_NULL(o);
    ASSERT_STR_CONTAINS(o, "hello");
    free(o);
    return 0;
}
static int t2(void) {
    char *o = NULL;
    int ec = -1;
    fork_exec_simple("exit 42", NULL, 5000, &o, &ec);
    ASSERT_INT_EQ(ec, 42);
    free(o);
    return 0;
}
static int t3(void) {
    char *o = NULL;
    int ec = -1;
    fork_exec_simple("pwd", "/tmp", 5000, &o, &ec);
    ASSERT_INT_EQ(ec, 0);
    ASSERT_NOT_NULL(o);
    ASSERT(strstr(o, "tmp") != NULL || strstr(o, "private") != NULL);
    free(o);
    return 0;
}
static int t4(void) {
    const char *args[] = {"-c", "echo test_pipe"};
    fork_request_t req;
    memset(&req, 0, sizeof(req));
    req.program = "/bin/sh";
    req.args = args;
    req.argc = 2;
    req.stdio_mode = FORK_STDIO_PIPE;
    fork_child_t child;
    ASSERT_INT_EQ(fork_spawn(&req, &child), 0);
    char *out = NULL;
    fork_read_output(&child, &out, 4096);
    fork_wait(&child, 5000);
    ASSERT_TRUE(child.exited);
    ASSERT_INT_EQ(child.exit_code, 0);
    ASSERT_STR_CONTAINS(out, "test_pipe");
    free(out);
    fork_child_cleanup(&child);
    return 0;
}
static int t5(void) {
    fork_child_t child;
    ASSERT(fork_spawn(NULL, &child) < 0);
    return 0;
}
static int t6(void) {
    fork_child_t child;
    memset(&child, 0, sizeof(child));
    child.exited = true;
    ASSERT(fork_kill(&child, SIGTERM) < 0);
    return 0;
}
static int t7(void) {
    char *o = NULL;
    int ec = -1;
    int rc = fork_exec_simple("echo drain_ok && sleep 0.01", NULL, 5000, &o, &ec);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(ec, 0);
    ASSERT_NOT_NULL(o);
    ASSERT_STR_CONTAINS(o, "drain_ok");
    free(o);
    return 0;
}
static int t8(void) {
    char *o = NULL;
    int ec = -1;
    int rc = fork_exec_simple("echo 'line1'; echo 'line2'; echo 'line3'", NULL, 5000, &o, &ec);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(o);
    ASSERT_STR_CONTAINS(o, "line1");
    ASSERT_STR_CONTAINS(o, "line3");
    free(o);
    return 0;
}
static int t_merged_captures_stderr(void) {
    char *o = NULL;
    int ec = -1;
    int rc = fork_exec_simple("echo stdout_ok; echo stderr_ok >&2", NULL, 5000, &o, &ec);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(ec, 0);
    ASSERT_NOT_NULL(o);
    ASSERT_STR_CONTAINS(o, "stdout_ok");
    ASSERT_STR_CONTAINS(o, "stderr_ok");
    free(o);
    return 0;
}
static int t_merged_heavy_stderr(void) {
    char *o = NULL;
    int ec = -1;
    int rc = fork_exec_simple(
        "for i in $(seq 1 200); do echo \"stderr_line_$i\" >&2; done; echo done_ok", NULL, 10000,
        &o, &ec);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(ec, 0);
    ASSERT_NOT_NULL(o);
    ASSERT_STR_CONTAINS(o, "done_ok");
    ASSERT_STR_CONTAINS(o, "stderr_line_1");
    free(o);
    return 0;
}
static int t_pipe_separate_stderr(void) {
    const char *args[] = {"-c", "echo out_ok; echo err_ok >&2"};
    fork_request_t req;
    memset(&req, 0, sizeof(req));
    req.program = "/bin/sh";
    req.args = args;
    req.argc = 2;
    req.stdio_mode = FORK_STDIO_PIPE;
    fork_child_t child;
    ASSERT_INT_EQ(fork_spawn(&req, &child), 0);
    char *out = NULL;
    fork_read_output(&child, &out, 4096);
    fork_wait(&child, 5000);
    ASSERT_TRUE(child.exited);
    ASSERT_INT_EQ(child.exit_code, 0);
    ASSERT_STR_CONTAINS(out, "out_ok");
    ASSERT(child.stderr_fd >= 0);
    free(out);
    fork_child_cleanup(&child);
    return 0;
}
int test_fork_backend_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("fork_backend");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t_merged_captures_stderr);
    RUN_TEST(t_merged_heavy_stderr);
    RUN_TEST(t_pipe_separate_stderr);
    TEST_RETURN_FAILED();
}
