#include "test.h"
#include "tools.h"
#include "nevercode.h"

static int test_search_by_name(void) {
    tools_register_all();
    const tool_def_t *results[32];
    int n = tools_search("bash", results, 32);
    ASSERT(n >= 1);
    ASSERT(strcmp(results[0]->name, "Bash") == 0);
    return 0;
}

static int test_search_by_desc(void) {
    const tool_def_t *results[32];
    int n = tools_search("file", results, 32);
    ASSERT(n >= 2);
    return 0;
}

static int test_search_case_insensitive(void) {
    const tool_def_t *results[32];
    int n1 = tools_search("GREP", results, 32);
    ASSERT(n1 >= 1);
    int n2 = tools_search("grep", results, 32);
    ASSERT_INT_EQ(n1, n2);
    return 0;
}

static int test_search_no_match(void) {
    const tool_def_t *results[32];
    int n = tools_search("xyzzy_nonexistent_12345", results, 32);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

static int test_search_tool_exec(void) {
    const tool_def_t *t = tools_find("ToolSearch");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "query", "search");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "Grep") != NULL || strstr(output, "Glob") != NULL ||
           strstr(output, "ToolSearch") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_search_tool_missing_query(void) {
    const tool_def_t *t = tools_find("ToolSearch");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT(strstr(output, "missing") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_search_tool_no_results(void) {
    const tool_def_t *t = tools_find("ToolSearch");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "query", "xyzzy_none_98765");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "No tools matching") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_tool_search_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_search");
    RUN_TEST(test_search_by_name);
    RUN_TEST(test_search_by_desc);
    RUN_TEST(test_search_case_insensitive);
    RUN_TEST(test_search_no_match);
    RUN_TEST(test_search_tool_exec);
    RUN_TEST(test_search_tool_missing_query);
    RUN_TEST(test_search_tool_no_results);
    TEST_RETURN_FAILED();
}
