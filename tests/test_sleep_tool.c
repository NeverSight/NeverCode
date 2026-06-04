#include "test.h"
#include "tools.h"
#include "nevercode.h"

static int test_register(void) {
    tool_sleep_register();
    const tool_def_t *t = tools_find("Sleep");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("Sleep");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_NOT_NULL(t->description);
    ASSERT_TRUE(t->read_only);
    ASSERT_TRUE(t->concurrency_safe);
    return 0;
}

static int test_exec_missing_seconds(void) {
    const tool_def_t *t = tools_find("Sleep");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "seconds");
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_exec_with_reason(void) {
    const tool_def_t *t = tools_find("Sleep");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddNumberToObject(input, "seconds", 1);
    cJSON_AddStringToObject(input, "reason", "test wait");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "1 second");
    ASSERT_STR_CONTAINS(output, "test wait");
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_sleep_tool_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("sleep_tool");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_exec_missing_seconds);
    RUN_TEST(test_exec_with_reason);
    TEST_RETURN_FAILED();
}
