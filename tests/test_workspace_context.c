#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_build_non_null(void) {
    char cwd[4096];
    ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char *ctx = workspace_context_build(cwd);
    ASSERT_NOT_NULL(ctx);
    ASSERT(strlen(ctx) > 0);
    free(ctx);
    return 0;
}
static int test_contains_cwd(void) {
    char cwd[4096];
    ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char *ctx = workspace_context_build(cwd);
    ASSERT_NOT_NULL(ctx);
    ASSERT(strstr(ctx, cwd) != NULL);
    free(ctx);
    return 0;
}
static int test_contains_tree(void) {
    char cwd[4096];
    ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char *ctx = workspace_context_build(cwd);
    ASSERT_NOT_NULL(ctx);
    ASSERT(strstr(ctx, "Working directory tree") != NULL);
    free(ctx);
    return 0;
}
static int test_null_cwd(void) {
    ASSERT_NULL(workspace_context_build(NULL));
    return 0;
}
static int test_tree_lines_basic(void) {
    char cwd[4096];
    ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char buf[16384];
    int lines = workspace_context_tree_lines(cwd, buf, sizeof(buf));
    ASSERT(lines > 0);
    ASSERT(strlen(buf) > 0);
    return 0;
}
static int test_tree_lines_null(void) {
    char buf[256];
    ASSERT_INT_EQ(workspace_context_tree_lines(NULL, buf, sizeof(buf)), 0);
    return 0;
}
static int test_filters_noisy(void) {
    char cwd[4096];
    ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char *ctx = workspace_context_build(cwd);
    ASSERT_NOT_NULL(ctx);
    ASSERT(strstr(ctx, "- node_modules/") == NULL);
    ASSERT(strstr(ctx, "- .git/") == NULL);
    free(ctx);
    return 0;
}
static int test_contains_header(void) {
    char cwd[4096];
    ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char *ctx = workspace_context_build(cwd);
    ASSERT_NOT_NULL(ctx);
    ASSERT(strstr(ctx, "Startup context") != NULL);
    free(ctx);
    return 0;
}
static int test_contains_notes(void) {
    char cwd[4096];
    ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);
    char *ctx = workspace_context_build(cwd);
    ASSERT_NOT_NULL(ctx);
    ASSERT(strstr(ctx, "## Notes") != NULL);
    free(ctx);
    return 0;
}
int test_workspace_context_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("workspace_context");
    RUN_TEST(test_build_non_null);
    RUN_TEST(test_contains_cwd);
    RUN_TEST(test_contains_tree);
    RUN_TEST(test_null_cwd);
    RUN_TEST(test_tree_lines_basic);
    RUN_TEST(test_tree_lines_null);
    RUN_TEST(test_filters_noisy);
    RUN_TEST(test_contains_header);
    RUN_TEST(test_contains_notes);
    TEST_RETURN_FAILED();
}
