#include "test.h"
#include "tools.h"

static int test_register(void) {
    tool_glob_register();
    const tool_def_t *t = tools_find("Glob");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("Glob");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_TRUE(t->read_only);
    return 0;
}

static int test_glob_c_files(void) {
    const tool_def_t *t = tools_find("Glob");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "pattern", "*.c");
    cJSON_AddStringToObject(input, "path", "src/core");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "main.c");
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_glob_no_match(void) {
    const tool_def_t *t = tools_find("Glob");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "pattern", "*.xyz_nonexistent");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_glob_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("glob");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_glob_c_files);
    RUN_TEST(test_glob_no_match);
    TEST_RETURN_FAILED();
}
