/*
 * test_session_scratch.c — Tests for per-session scratch directory.
 */
#include "test.h"
#include "nevercode.h"

static int t_init_creates_dir(void) {
    session_scratch_cleanup();
    ASSERT(!session_scratch_active());
    ASSERT_NULL(session_scratch_dir());

    int rc = session_scratch_init("test-scratch-123");
    ASSERT_INT_EQ(rc, 0);
    ASSERT(session_scratch_active());
    ASSERT_NOT_NULL(session_scratch_dir());
    ASSERT(strstr(session_scratch_dir(), "test-scratch-123") != NULL);

    session_scratch_cleanup();
    ASSERT(!session_scratch_active());
    return 0;
}

static int t_file_path(void) {
    session_scratch_init("test-scratch-path");
    char *p = session_scratch_file("tmp.txt");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "tmp.txt") != NULL);
    ASSERT(strstr(p, "test-scratch-path") != NULL);
    free(p);

    char *nil = session_scratch_file(NULL);
    ASSERT_NULL(nil);

    session_scratch_cleanup();
    return 0;
}

static int t_prompt_output(void) {
    session_scratch_init("test-scratch-prompt");
    char *pr = session_scratch_prompt();
    ASSERT_NOT_NULL(pr);
    ASSERT(strstr(pr, "Scratch Directory") != NULL);
    ASSERT(strstr(pr, "test-scratch-prompt") != NULL);
    free(pr);

    session_scratch_cleanup();

    char *nil = session_scratch_prompt();
    ASSERT_NULL(nil);
    return 0;
}

static int t_inactive_returns_null(void) {
    session_scratch_cleanup();
    ASSERT_NULL(session_scratch_dir());
    ASSERT_NULL(session_scratch_file("x"));
    ASSERT_NULL(session_scratch_prompt());
    return 0;
}

static int t_macros(void) {
    ASSERT(strlen(SESSION_SCRATCH_SUBDIR) > 0);
    return 0;
}

int test_session_scratch_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_scratch");
    RUN_TEST(t_init_creates_dir);
    RUN_TEST(t_file_path);
    RUN_TEST(t_prompt_output);
    RUN_TEST(t_inactive_returns_null);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
