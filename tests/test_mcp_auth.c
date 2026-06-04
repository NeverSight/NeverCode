#include "test.h"
#include "tools.h"
#include "nevercode.h"

static int test_register(void) {
    tool_mcp_auth_register();
    const tool_def_t *t = tools_find("McpAuth");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("McpAuth");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_NOT_NULL(t->description);
    ASSERT_TRUE(strlen(t->description) > 0);
    return 0;
}

static int test_exec_missing_server(void) {
    const tool_def_t *t = tools_find("McpAuth");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "server_name");
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_exec_success(void) {
    const tool_def_t *t = tools_find("McpAuth");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "server_name", "test_server");
    cJSON_AddStringToObject(input, "auth_type", "api_key");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "authenticated");
    ASSERT_STR_CONTAINS(output, "test_server");
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_exec_default_auth_type(void) {
    const tool_def_t *t = tools_find("McpAuth");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "server_name", "srv");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "token");
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_mcp_auth_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("mcp_auth");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_exec_missing_server);
    RUN_TEST(test_exec_success);
    RUN_TEST(test_exec_default_auth_type);
    TEST_RETURN_FAILED();
}
