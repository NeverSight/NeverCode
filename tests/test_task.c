#include "test.h"
#include "tools.h"

static int test_register(void) {
    tool_task_register();
    const tool_def_t *t = tools_find("TaskCreate");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_list(void) {
    const tool_def_t *t = tools_find("TaskList");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    return 0;
}

static int test_schema_create(void) {
    const tool_def_t *t = tools_find("TaskCreate");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_NOT_NULL(t->description);
    return 0;
}

static int test_not_readonly(void) {
    const tool_def_t *t = tools_find("TaskCreate");
    ASSERT_NOT_NULL(t);
    ASSERT_FALSE(t->read_only);
    return 0;
}

int test_task_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("task");
    RUN_TEST(test_register);
    RUN_TEST(test_list);
    RUN_TEST(test_schema_create);
    RUN_TEST(test_not_readonly);
    TEST_RETURN_FAILED();
}
