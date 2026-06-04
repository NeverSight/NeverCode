#include "test.h"
#include "nevercode.h"
#include "tools.h"

static int t_register(void) {
    snip_register_tool();
    const tool_def_t *t = tools_find("Snip");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->name, "Snip");
    ASSERT_TRUE(t->read_only);
    return 0;
}

static int t_valid_range(void) {
    const tool_def_t *t = tools_find("Snip");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddNumberToObject(input, "from_index", 2);
    cJSON_AddNumberToObject(input, "to_index", 5);
    cJSON_AddStringToObject(input, "reason", "old tool results");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "4 message(s)");
    ASSERT_STR_CONTAINS(output, "old tool results");
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_missing_params(void) {
    const tool_def_t *t = tools_find("Snip");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, -1);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Error");
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_invalid_range(void) {
    const tool_def_t *t = tools_find("Snip");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddNumberToObject(input, "from_index", 10);
    cJSON_AddNumberToObject(input, "to_index", 5);

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, -1);
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_snip_tool_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("snip_tool");
    RUN_TEST(t_register);
    RUN_TEST(t_valid_range);
    RUN_TEST(t_missing_params);
    RUN_TEST(t_invalid_range);
    TEST_RETURN_FAILED();
}
