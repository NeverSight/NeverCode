#include "test.h"
#include "tools.h"
#include "nevercode.h"
#include <stdio.h>

extern bool g_brief_mode;

static int test_brief_tool_registered(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("Brief");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);
    return 0;
}

static int test_brief_sends_message(void) {
    g_brief_mode = false;

    const tool_def_t *t = tools_find("Brief");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "message", "Build succeeded with 0 errors.");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(output, "Build succeeded with 0 errors.");
    ASSERT(g_brief_mode == true);
    free(output);

    g_brief_mode = false;
    return 0;
}

static int test_brief_missing_message(void) {
    const tool_def_t *t = tools_find("Brief");
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "missing") != NULL);
    free(output);
    return 0;
}

int test_brief_tool_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("brief_tool");

    RUN_TEST(test_brief_tool_registered);
    RUN_TEST(test_brief_sends_message);
    RUN_TEST(test_brief_missing_message);

    TEST_RETURN_FAILED();
}
