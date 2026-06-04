#include "test.h"
#include "tools.h"

static int test_register(void) {
    tool_listdir_register();
    const tool_def_t *t = tools_find("ListDir");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("ListDir");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_TRUE(t->read_only);
    return 0;
}

static int test_list_cwd(void) {
    const tool_def_t *t = tools_find("ListDir");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", ".");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Makefile");
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_listdir_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("listdir");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_list_cwd);
    TEST_RETURN_FAILED();
}
