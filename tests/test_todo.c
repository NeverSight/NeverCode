#include "test.h"
#include "tools.h"

static int test_register(void) {
    tool_todo_register();
    const tool_def_t *t = tools_find("TodoWrite");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("TodoWrite");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_NOT_NULL(t->description);
    return 0;
}

static int test_not_readonly(void) {
    const tool_def_t *t = tools_find("TodoWrite");
    ASSERT_NOT_NULL(t);
    ASSERT_FALSE(t->read_only);
    return 0;
}

static int test_exec_empty(void) {
    const tool_def_t *t = tools_find("TodoWrite");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON *todos = cJSON_CreateArray();
    cJSON_AddItemToObject(input, "todos", todos);
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_todo_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("todo");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_not_readonly);
    RUN_TEST(test_exec_empty);
    TEST_RETURN_FAILED();
}
