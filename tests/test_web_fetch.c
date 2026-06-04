#include "test.h"
#include "tools.h"

static int test_register(void) {
    tool_web_fetch_register();
    const tool_def_t *t = tools_find("WebFetch");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("WebFetch");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_NOT_NULL(t->description);
    return 0;
}

static int test_exec_missing_url(void) {
    const tool_def_t *t = tools_find("WebFetch");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_web_fetch_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("web_fetch");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_exec_missing_url);
    TEST_RETURN_FAILED();
}
