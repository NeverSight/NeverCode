#include "test.h"
#include "tools.h"

static int test_find_missing(void) {
    const tool_def_t *f = tools_find("_nonexistent_xyz_");
    ASSERT_NULL(f);
    return 0;
}

static int test_count(void) {
    tools_register_all();
    ASSERT_GT(tools_count(), 0);
    return 0;
}

static int test_get_all(void) {
    tools_register_all();
    const tool_def_t *all = tools_get_all();
    ASSERT_NOT_NULL(all);
    return 0;
}

static int test_find_bash(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("Bash");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->name, "Bash");
    ASSERT_NOT_NULL(t->description);
    ASSERT_NOT_NULL(t->input_schema);
    return 0;
}

static int test_find_read(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("Read");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->name, "Read");
    return 0;
}

static int test_find_case_sensitive(void) {
    tools_register_all();
    ASSERT_NULL(tools_find("bash"));
    ASSERT_NULL(tools_find("BASH"));
    return 0;
}

int test_tool_registry_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_registry");
    RUN_TEST(test_find_missing);
    RUN_TEST(test_count);
    RUN_TEST(test_get_all);
    RUN_TEST(test_find_bash);
    RUN_TEST(test_find_read);
    RUN_TEST(test_find_case_sensitive);
    TEST_RETURN_FAILED();
}
