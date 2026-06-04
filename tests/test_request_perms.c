#include "test.h"
#include "nevercode.h"
#include "permission.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

extern int request_perms_execute(const cJSON *input, char **output, void *ctx);

static int test_request_perms_null_input(void) {
    char *output = NULL;
    int rc = request_perms_execute(NULL, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "missing input") != NULL);
    free(output);
    return 0;
}

static int test_request_perms_missing_permissions(void) {
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = request_perms_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "must be a JSON array") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_request_perms_empty_permissions(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddArrayToObject(input, "permissions");
    char *output = NULL;
    int rc = request_perms_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "at least one") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_request_perms_schema(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON *perms = cJSON_AddArrayToObject(input, "permissions");
    cJSON *perm = cJSON_CreateObject();
    cJSON_AddStringToObject(perm, "tool", "Bash");
    cJSON_AddStringToObject(perm, "path", "/usr/local");
    cJSON_AddItemToArray(perms, perm);
    cJSON_AddStringToObject(input, "reason", "need to install deps");

    ASSERT_NOT_NULL(cJSON_GetObjectItem(input, "permissions"));
    ASSERT(cJSON_IsArray(cJSON_GetObjectItem(input, "permissions")));
    ASSERT_INT_EQ(cJSON_GetArraySize(perms), 1);

    cJSON *item = cJSON_GetArrayItem(perms, 0);
    ASSERT_STR_EQ(cJSON_GetObjectItem(item, "tool")->valuestring, "Bash");
    ASSERT_STR_EQ(cJSON_GetObjectItem(item, "path")->valuestring, "/usr/local");
    ASSERT_STR_EQ(cJSON_GetObjectItem(input, "reason")->valuestring, "need to install deps");

    cJSON_Delete(input);
    return 0;
}

static int test_request_perms_tool_registered(void) {
    extern void tools_register_all(void);
    extern const tool_def_t *tools_find(const char *name);
    tools_register_all();
    const tool_def_t *t = tools_find("RequestPermissions");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == false);
    ASSERT(t->destructive == false);
    return 0;
}

int test_request_perms_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("request_perms");
    RUN_TEST(test_request_perms_null_input);
    RUN_TEST(test_request_perms_missing_permissions);
    RUN_TEST(test_request_perms_empty_permissions);
    RUN_TEST(test_request_perms_schema);
    RUN_TEST(test_request_perms_tool_registered);
    TEST_RETURN_FAILED();
}
