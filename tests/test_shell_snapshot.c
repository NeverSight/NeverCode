#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_snapshot_capture(void) {
    shell_snapshot_capture();
    char *summary = shell_snapshot_summary();
    ASSERT_NOT_NULL(summary);
    ASSERT(strstr(summary, "Shell state") != NULL);
    ASSERT(strstr(summary, "cwd:") != NULL);
    free(summary);
    return 0;
}

static int test_snapshot_summary_contains_cwd(void) {
    shell_snapshot_capture();
    char *summary = shell_snapshot_summary();
    ASSERT_NOT_NULL(summary);
    ASSERT(strstr(summary, "/") != NULL);
    free(summary);
    return 0;
}

static int test_snapshot_no_capture(void) {
    shell_snapshot_free();
    char *summary = shell_snapshot_summary();
    ASSERT_NOT_NULL(summary);
    ASSERT(strstr(summary, "no shell snapshot") != NULL);
    free(summary);
    return 0;
}

static int test_snapshot_free(void) {
    shell_snapshot_capture();
    shell_snapshot_free();
    char *summary = shell_snapshot_summary();
    ASSERT(strstr(summary, "no shell snapshot") != NULL);
    free(summary);
    return 0;
}

int test_shell_snapshot_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("shell_snapshot");

    RUN_TEST(test_snapshot_capture);
    RUN_TEST(test_snapshot_summary_contains_cwd);
    RUN_TEST(test_snapshot_no_capture);
    RUN_TEST(test_snapshot_free);

    TEST_RETURN_FAILED();
}
