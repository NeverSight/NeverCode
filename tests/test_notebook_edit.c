#include "test.h"
#include "tools.h"

static int test_register(void) {
    tool_notebook_edit_register();
    const tool_def_t *t = tools_find("NotebookEdit");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("NotebookEdit");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_NOT_NULL(t->description);
    return 0;
}

static int test_not_readonly(void) {
    const tool_def_t *t = tools_find("NotebookEdit");
    ASSERT_NOT_NULL(t);
    ASSERT_FALSE(t->read_only);
    return 0;
}

int test_notebook_edit_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("notebook_edit");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_not_readonly);
    TEST_RETURN_FAILED();
}
