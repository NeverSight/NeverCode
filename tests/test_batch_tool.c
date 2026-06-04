#include "test.h"
#include "tools.h"

static int t_registration(void) {
    tools_register_all();
    const tool_def_t *tool = tools_find("Batch");
    ASSERT_NOT_NULL(tool);
    ASSERT_STR_EQ(tool->name, "Batch");
    ASSERT_NOT_NULL(tool->input_schema);
    return 0;
}

static int t_empty_calls(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddArrayToObject(input, "tool_calls");

    char *output = NULL;
    const tool_def_t *tool = tools_find("Batch");
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_unknown_tool(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON *calls = cJSON_AddArrayToObject(input, "tool_calls");

    cJSON *call = cJSON_CreateObject();
    cJSON_AddStringToObject(call, "tool", "NonExistentTool12345");
    cJSON_AddItemToObject(call, "parameters", cJSON_CreateObject());
    cJSON_AddItemToArray(calls, call);

    char *output = NULL;
    const tool_def_t *tool = tools_find("Batch");
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(strstr(output, "not found"));
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_disallowed(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON *calls = cJSON_AddArrayToObject(input, "tool_calls");

    cJSON *call = cJSON_CreateObject();
    cJSON_AddStringToObject(call, "tool", "Batch");
    cJSON_AddItemToObject(call, "parameters", cJSON_CreateObject());
    cJSON_AddItemToArray(calls, call);

    char *output = NULL;
    const tool_def_t *tool = tools_find("Batch");
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(strstr(output, "cannot be used"));
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_missing_params(void) {
    cJSON *input = cJSON_CreateObject();

    char *output = NULL;
    const tool_def_t *tool = tools_find("Batch");
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_missing_tool_name(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON *calls = cJSON_AddArrayToObject(input, "tool_calls");

    cJSON *call = cJSON_CreateObject();
    cJSON_AddItemToObject(call, "parameters", cJSON_CreateObject());
    cJSON_AddItemToArray(calls, call);

    char *output = NULL;
    const tool_def_t *tool = tools_find("Batch");
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(strstr(output, "missing tool name"));
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_batch_tool_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("batch_tool");
    RUN_TEST(t_registration);
    RUN_TEST(t_empty_calls);
    RUN_TEST(t_unknown_tool);
    RUN_TEST(t_disallowed);
    RUN_TEST(t_missing_params);
    RUN_TEST(t_missing_tool_name);
    TEST_RETURN_FAILED();
}
