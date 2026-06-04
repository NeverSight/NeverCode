#include "test.h"
#include "tools.h"

static int test_register(void) {
    tool_git_diff_register();
    const tool_def_t *t = tools_find("GitDiff");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("GitDiff");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_NOT_NULL(t->description);
    return 0;
}

static int test_readonly(void) {
    const tool_def_t *t = tools_find("GitDiff");
    ASSERT_NOT_NULL(t);
    ASSERT_TRUE(t->read_only);
    return 0;
}

int test_git_diff_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("git_diff");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_readonly);
    TEST_RETURN_FAILED();
}
