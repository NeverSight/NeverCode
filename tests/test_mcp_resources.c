#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include "mcp.h"
#include "repl.h"

static int test_mcp_resource_tools_registered(void) {
    const tool_def_t *list_t = tools_find("ListMcpResources");
    ASSERT_NOT_NULL(list_t);
    ASSERT(list_t->read_only == true);
    ASSERT(list_t->concurrency_safe == true);
    ASSERT_NOT_NULL(list_t->execute);

    const tool_def_t *read_t = tools_find("ReadMcpResource");
    ASSERT_NOT_NULL(read_t);
    ASSERT(read_t->read_only == true);
    ASSERT(read_t->concurrency_safe == true);
    ASSERT_NOT_NULL(read_t->execute);
    return 0;
}

static int test_list_resources_no_servers(void) {
    const tool_def_t *t = tools_find("ListMcpResources");
    ASSERT_NOT_NULL(t);

    int saved = g_num_mcp_servers;
    g_num_mcp_servers = 0;

    char *output = NULL;
    int rc = t->execute(NULL, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "No MCP servers") != NULL);
    free(output);

    g_num_mcp_servers = saved;
    return 0;
}

static int test_read_resource_no_servers(void) {
    const tool_def_t *t = tools_find("ReadMcpResource");
    ASSERT_NOT_NULL(t);

    int saved = g_num_mcp_servers;
    g_num_mcp_servers = 0;

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "uri", "test://resource");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "no MCP servers") != NULL);
    free(output);
    cJSON_Delete(input);

    g_num_mcp_servers = saved;
    return 0;
}

static int test_read_resource_missing_uri(void) {
    const tool_def_t *t = tools_find("ReadMcpResource");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "uri") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_list_resources_schema(void) {
    const tool_def_t *t = tools_find("ListMcpResources");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);

    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(t->input_schema, "type"));
    ASSERT_STR_EQ(type, "object");
    return 0;
}

static int test_read_resources_schema(void) {
    const tool_def_t *t = tools_find("ReadMcpResource");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);

    cJSON *props = cJSON_GetObjectItem(t->input_schema, "properties");
    ASSERT_NOT_NULL(props);
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "uri"));
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "server"));

    cJSON *req = cJSON_GetObjectItem(t->input_schema, "required");
    ASSERT_NOT_NULL(req);
    ASSERT_INT_EQ(cJSON_GetArraySize(req), 1);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetArrayItem(req, 0)), "uri");
    return 0;
}

int test_mcp_resources_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("mcp_resources");
    RUN_TEST(test_mcp_resource_tools_registered);
    RUN_TEST(test_list_resources_no_servers);
    RUN_TEST(test_read_resource_no_servers);
    RUN_TEST(test_read_resource_missing_uri);
    RUN_TEST(test_list_resources_schema);
    RUN_TEST(test_read_resources_schema);
    TEST_RETURN_FAILED();
}
