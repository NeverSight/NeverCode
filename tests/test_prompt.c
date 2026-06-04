#include "test.h"
#include "nevercode.h"

static int test_build(void) {
    char *p = build_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT(strlen(p) > 0);
    free(p);
    return 0;
}

static int test_contains_tool_info(void) {
    char *p = build_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "tool");
    free(p);
    return 0;
}

static int test_contains_identity(void) {
    char *p = build_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, NC_PRODUCT_NAME);
    free(p);
    return 0;
}

static int test_nonempty(void) {
    char *p = build_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT_GT((int)strlen(p), 100);
    free(p);
    return 0;
}

static int test_autonomy_principles(void) {
    char *p = build_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "Persist until the task is fully handled end-to-end");
    ASSERT_STR_CONTAINS(p, "smallest correct changes");
    free(p);
    return 0;
}

static int test_git_safety(void) {
    char *p = build_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "non-interactive git commands");
    ASSERT_STR_CONTAINS(p, "NEVER revert changes you did not make");
    ASSERT_STR_CONTAINS(p, "git reset --hard");
    free(p);
    return 0;
}

static int test_dev_tools_detected(void) {
    char *p = build_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "Dev tools:");
    free(p);
    return 0;
}

static int test_env_info(void) {
    char *p = build_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "Working directory:");
    ASSERT_STR_CONTAINS(p, "OS:");
    ASSERT_STR_CONTAINS(p, "Date:");
    free(p);
    return 0;
}

int test_prompt_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("prompt");
    RUN_TEST(test_build);
    RUN_TEST(test_contains_tool_info);
    RUN_TEST(test_contains_identity);
    RUN_TEST(test_nonempty);
    RUN_TEST(test_autonomy_principles);
    RUN_TEST(test_git_safety);
    RUN_TEST(test_dev_tools_detected);
    RUN_TEST(test_env_info);
    TEST_RETURN_FAILED();
}
