#include "test.h"
#include "tools.h"
#include "nevercode.h"

static int test_worktree_tools_registered(void) {
    tools_register_all();
    const tool_def_t *enter = tools_find("EnterWorktree");
    const tool_def_t *exit_wt = tools_find("ExitWorktree");
    ASSERT_NOT_NULL(enter);
    ASSERT_NOT_NULL(exit_wt);
    ASSERT(enter->execute != NULL);
    ASSERT(exit_wt->execute != NULL);
    return 0;
}

static int test_enter_worktree_flags(void) {
    const tool_def_t *t = tools_find("EnterWorktree");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == false);
    ASSERT(t->concurrency_safe == false);
    ASSERT(t->destructive == false);
    return 0;
}

static int test_exit_worktree_flags(void) {
    const tool_def_t *t = tools_find("ExitWorktree");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == false);
    ASSERT(t->concurrency_safe == false);
    ASSERT(t->destructive == false);
    return 0;
}

static int test_enter_worktree_missing_branch(void) {
    const tool_def_t *t = tools_find("EnterWorktree");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT(strstr(output, "missing") != NULL || strstr(output, "branch") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_exit_worktree_not_in(void) {
    const tool_def_t *t = tools_find("ExitWorktree");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Not currently") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_enter_worktree_schema(void) {
    const tool_def_t *t = tools_find("EnterWorktree");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    cJSON *props = cJSON_GetObjectItem(t->input_schema, "properties");
    ASSERT_NOT_NULL(props);
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "branch"));
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "path"));
    return 0;
}

int test_worktree_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("worktree");
    RUN_TEST(test_worktree_tools_registered);
    RUN_TEST(test_enter_worktree_flags);
    RUN_TEST(test_exit_worktree_flags);
    RUN_TEST(test_enter_worktree_missing_branch);
    RUN_TEST(test_exit_worktree_not_in);
    RUN_TEST(test_enter_worktree_schema);
    TEST_RETURN_FAILED();
}
