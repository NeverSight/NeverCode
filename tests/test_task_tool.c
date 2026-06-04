#include "test.h"
#include "nevercode.h"
#include "tools.h"

static int test_task_tools_registered(void) {
    ASSERT(tools_find("TaskCreate") != NULL);
    ASSERT(tools_find("TaskGet") != NULL);
    ASSERT(tools_find("TaskList") != NULL);
    ASSERT(tools_find("TaskStop") != NULL);
    ASSERT(tools_find("SendMessage") != NULL);
    return 0;
}

static int test_task_create_missing_prompt(void) {
    const tool_def_t *tool = tools_find("TaskCreate");
    ASSERT_NOT_NULL(tool);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "description", "test");
    char *output = NULL;
    int rc = tool->execute(input, &output, NULL);
    ASSERT(rc == 1);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "error") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_task_list_empty(void) {
    const tool_def_t *tool = tools_find("TaskList");
    ASSERT_NOT_NULL(tool);
    agent_pool_init(agent_pool_global());
    char *output = NULL;
    int rc = tool->execute(NULL, &output, NULL);
    ASSERT(rc == 0);
    ASSERT_NOT_NULL(output);
    free(output);
    return 0;
}

static int test_task_get_not_found(void) {
    const tool_def_t *tool = tools_find("TaskGet");
    ASSERT_NOT_NULL(tool);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "task_id", "nonexistent-id");
    char *output = NULL;
    int rc = tool->execute(input, &output, NULL);
    ASSERT(rc == 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "not_found") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_task_stop_not_found(void) {
    const tool_def_t *tool = tools_find("TaskStop");
    ASSERT_NOT_NULL(tool);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "task_id", "nonexistent-id");
    char *output = NULL;
    int rc = tool->execute(input, &output, NULL);
    ASSERT(rc == 1);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_send_message_missing_fields(void) {
    const tool_def_t *tool = tools_find("SendMessage");
    ASSERT_NOT_NULL(tool);
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = tool->execute(input, &output, NULL);
    ASSERT(rc == 1);
    ASSERT(strstr(output, "error") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_task_schema_valid(void) {
    const tool_def_t *tc = tools_find("TaskCreate");
    ASSERT_NOT_NULL(tc);
    ASSERT_NOT_NULL(tc->input_schema);
    cJSON *props = cJSON_GetObjectItem(tc->input_schema, "properties");
    ASSERT_NOT_NULL(props);
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "prompt"));
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "description"));
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "agent_type"));
    return 0;
}

int test_task_tool_all(void) {
    TEST_SUITE("task_tool");
    TEST_COUNTERS;

    tools_register_all();
    agent_pool_init(agent_pool_global());

    RUN_TEST(test_task_tools_registered);
    RUN_TEST(test_task_create_missing_prompt);
    RUN_TEST(test_task_list_empty);
    RUN_TEST(test_task_get_not_found);
    RUN_TEST(test_task_stop_not_found);
    RUN_TEST(test_send_message_missing_fields);
    RUN_TEST(test_task_schema_valid);

    return _t_failed;
}
