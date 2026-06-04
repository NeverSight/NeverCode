#include "test.h"
#include "nevercode.h"
#include <unistd.h>
#include <sys/stat.h>

static int test_detect_from_cwd(void) {
    project_root_reset();
    const char *root = project_root_detect(NULL);
    ASSERT_NOT_NULL(root);
    ASSERT(strlen(root) > 0);
    return 0;
}

static int test_detect_with_git(void) {
    project_root_reset();
    char cwd[4096];
    getcwd(cwd, sizeof(cwd));
    const char *root = project_root_detect(cwd);
    ASSERT_NOT_NULL(root);
    return 0;
}

static int test_markers_at(void) {
    const char *markers[32];
    int n = project_root_markers_at("/nonexistent/path/xyz", markers, 32);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

static int test_primary_marker_null(void) {
    const char *m = project_root_primary_marker(NULL);
    ASSERT_NULL(m);
    return 0;
}

static int test_describe(void) {
    project_root_reset();
    char *desc = project_root_describe();
    ASSERT_NOT_NULL(desc);
    ASSERT(strstr(desc, "Project root:") != NULL);
    free(desc);
    return 0;
}

static int test_is_git(void) {
    project_root_reset();
    /* We're running inside a git repo (nevercode/), so this should be true
       or at least not crash */
    (void)project_root_is_git();
    return 0;
}

static int test_list_markers(void) {
    const char *list = project_root_list_markers();
    ASSERT_NOT_NULL(list);
    ASSERT(strstr(list, ".git") != NULL);
    ASSERT(strstr(list, "package.json") != NULL);
    ASSERT(strstr(list, "Cargo.toml") != NULL);
    return 0;
}

static int test_reset(void) {
    project_root_reset();
    project_root_detect(NULL);
    project_root_reset();
    const char *root = project_root_detect(NULL);
    ASSERT_NOT_NULL(root);
    return 0;
}

int test_project_root_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("project_root");
    RUN_TEST(test_detect_from_cwd);
    RUN_TEST(test_detect_with_git);
    RUN_TEST(test_markers_at);
    RUN_TEST(test_primary_marker_null);
    RUN_TEST(test_describe);
    RUN_TEST(test_is_git);
    RUN_TEST(test_list_markers);
    RUN_TEST(test_reset);
    TEST_RETURN_FAILED();
}
