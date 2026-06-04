#include "test.h"
#include "defaults.h"
#include "tools.h"

static int test_register(void) {
    tool_grep_register();
    const tool_def_t *t = tools_find("Grep");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("Grep");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_TRUE(t->read_only);
    return 0;
}

static int test_grep_makefile(void) {
    const tool_def_t *t = tools_find("Grep");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "pattern", "nevercode");
    cJSON_AddStringToObject(input, "path", "Makefile");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_TRUE(strlen(output) > 0);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_grep_no_match(void) {
    const tool_def_t *t = tools_find("Grep");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "pattern", "zzz_impossible_pattern_xyz");
    cJSON_AddStringToObject(input, "path", "Makefile");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_max_matches_macro(void) {
    ASSERT_TRUE(GREP_MAX_MATCHES > 0);
    ASSERT_TRUE(GREP_MAX_MATCHES <= 10000);
    ASSERT_INT_EQ(GREP_MAX_MATCHES, 200);
    return 0;
}

static int test_grep_concurrency_safe(void) {
    const tool_def_t *t = tools_find("Grep");
    ASSERT_NOT_NULL(t);
    ASSERT_TRUE(t->concurrency_safe);
    ASSERT_TRUE(t->read_only);
    ASSERT_TRUE(!t->destructive);
    return 0;
}

int test_grep_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("grep");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_grep_makefile);
    RUN_TEST(test_grep_no_match);
    RUN_TEST(test_max_matches_macro);
    RUN_TEST(test_grep_concurrency_safe);
    TEST_RETURN_FAILED();
}
