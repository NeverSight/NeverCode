#include "test.h"
#include "nevercode.h"

static int test_ig_init(void) {
    input_guard_init("/tmp/project");
    ASSERT_INT_EQ(input_guard_checked_count(), 0);
    ASSERT_INT_EQ(input_guard_blocked_count(), 0);
    input_guard_free();
    return 0;
}

static int test_ig_normal_path(void) {
    input_guard_init("/tmp/project");
    ASSERT_TRUE(input_guard_check_path("src/main.c"));
    ASSERT_TRUE(input_guard_check_path("README.md"));
    ASSERT_TRUE(input_guard_check_path("/tmp/project/src/main.c"));
    ASSERT_INT_EQ(input_guard_blocked_count(), 0);
    input_guard_free();
    return 0;
}

static int test_ig_traversal_blocked(void) {
    input_guard_init("/tmp/project");
    ASSERT_FALSE(input_guard_check_path("../../../etc/passwd"));
    ASSERT_FALSE(input_guard_check_path("src/../../outside"));
    ASSERT_GT(input_guard_blocked_count(), 0);
    input_guard_free();
    return 0;
}

static int test_ig_null_input(void) {
    input_guard_init("/tmp/project");
    ASSERT_FALSE(input_guard_check_path(NULL));
    ASSERT_FALSE(input_guard_check_path(""));
    input_guard_free();
    return 0;
}

static int test_ig_absolute_denied(void) {
    input_guard_init("/tmp/project");
    input_guard_set_allow_absolute(false);
    ASSERT_FALSE(input_guard_check_path("/etc/passwd"));
    ASSERT_TRUE(input_guard_check_path("src/main.c"));
    input_guard_free();
    return 0;
}

static int test_ig_resolve_path(void) {
    input_guard_init("/tmp/project");
    char out[4096];
    char *r = input_guard_resolve("src/main.c", out, sizeof(out));
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(out, "/tmp/project/src/main.c");

    r = input_guard_resolve("../escape", out, sizeof(out));
    ASSERT_NULL(r);
    input_guard_free();
    return 0;
}

static int test_ig_diagnose(void) {
    input_guard_init("/tmp/project");
    ASSERT_STR_EQ(input_guard_diagnose("src/main.c"), "ok");
    ASSERT_STR_EQ(input_guard_diagnose("../../etc/passwd"), "path_traversal");
    ASSERT_STR_EQ(input_guard_diagnose(NULL), "invalid");
    input_guard_free();
    return 0;
}

static int test_ig_length_check(void) {
    input_guard_init("/tmp/project");
    ASSERT_TRUE(input_guard_check_length("short", 100));
    ASSERT_FALSE(input_guard_check_length("toolong", 3));
    ASSERT_TRUE(input_guard_check_length(NULL, 100));
    input_guard_free();
    return 0;
}

static int test_ig_format(void) {
    input_guard_init("/tmp/project");
    input_guard_check_path("src/main.c");
    input_guard_check_path("../escape");
    char *f = input_guard_format();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Input Guard");
    ASSERT_STR_CONTAINS(f, "checked=2");
    ASSERT_STR_CONTAINS(f, "blocked=1");
    free(f);
    input_guard_free();
    return 0;
}

int test_input_guard_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("input_guard");
    RUN_TEST(test_ig_init);
    RUN_TEST(test_ig_normal_path);
    RUN_TEST(test_ig_traversal_blocked);
    RUN_TEST(test_ig_null_input);
    RUN_TEST(test_ig_absolute_denied);
    RUN_TEST(test_ig_resolve_path);
    RUN_TEST(test_ig_diagnose);
    RUN_TEST(test_ig_length_check);
    RUN_TEST(test_ig_format);
    TEST_RETURN_FAILED();
}
