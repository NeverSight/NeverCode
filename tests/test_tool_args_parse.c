/*
 * test_tool_args_parse.c — Tests for lenient tool-argument JSON parser.
 */

#include "test.h"
#include "nevercode.h"

static int test_valid_json_passthrough(void) {
    cJSON *j = tool_args_parse_json("{\"path\":\"/tmp/file.txt\"}");
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(j, "path")), "/tmp/file.txt");
    cJSON_Delete(j);
    return 0;
}

static int test_null_and_empty(void) {
    ASSERT_NULL(tool_args_parse_json(NULL));
    ASSERT_NULL(tool_args_parse_json(""));
    return 0;
}

static int test_markdown_fence_json(void) {
    const char *input = "```json\n{\"command\":\"ls -la\"}\n```";
    cJSON *j = tool_args_parse_json(input);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(j, "command")), "ls -la");
    cJSON_Delete(j);
    return 0;
}

static int test_markdown_fence_no_lang(void) {
    const char *input = "```\n{\"path\":\"/etc\"}\n```";
    cJSON *j = tool_args_parse_json(input);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(j, "path")), "/etc");
    cJSON_Delete(j);
    return 0;
}

static int test_trailing_comma_object(void) {
    const char *input = "{\"a\":\"1\",\"b\":\"2\",}";
    cJSON *j = tool_args_parse_json(input);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(j, "a")), "1");
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(j, "b")), "2");
    cJSON_Delete(j);
    return 0;
}

static int test_trailing_comma_array(void) {
    const char *input = "{\"items\":[1,2,3,]}";
    cJSON *j = tool_args_parse_json(input);
    ASSERT_NOT_NULL(j);
    cJSON *items = cJSON_GetObjectItem(j, "items");
    ASSERT_NOT_NULL(items);
    ASSERT_INT_EQ(cJSON_GetArraySize(items), 3);
    cJSON_Delete(j);
    return 0;
}

static int test_bare_keys(void) {
    const char *input = "{command:\"echo hello\"}";
    cJSON *j = tool_args_parse_json(input);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(j, "command")), "echo hello");
    cJSON_Delete(j);
    return 0;
}

static int test_bare_keys_multiple(void) {
    const char *input = "{path:\"/tmp\",recursive:true}";
    cJSON *j = tool_args_parse_json(input);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(j, "path")), "/tmp");
    ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(j, "recursive")));
    cJSON_Delete(j);
    return 0;
}

static int test_fence_plus_trailing_comma(void) {
    const char *input = "```json\n{\"url\":\"https://example.com\",}\n```";
    cJSON *j = tool_args_parse_json(input);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(j, "url")), "https://example.com");
    cJSON_Delete(j);
    return 0;
}

static int test_whitespace_around(void) {
    const char *input = "  \n  {\"key\":\"val\"}  \n  ";
    cJSON *j = tool_args_parse_json(input);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(j, "key")), "val");
    cJSON_Delete(j);
    return 0;
}

static int test_garbage_returns_null(void) {
    ASSERT_NULL(tool_args_parse_json("not json at all"));
    ASSERT_NULL(tool_args_parse_json("{{{{"));
    return 0;
}

int test_tool_args_parse_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_args_parse");
    RUN_TEST(test_valid_json_passthrough);
    RUN_TEST(test_null_and_empty);
    RUN_TEST(test_markdown_fence_json);
    RUN_TEST(test_markdown_fence_no_lang);
    RUN_TEST(test_trailing_comma_object);
    RUN_TEST(test_trailing_comma_array);
    RUN_TEST(test_bare_keys);
    RUN_TEST(test_bare_keys_multiple);
    RUN_TEST(test_fence_plus_trailing_comma);
    RUN_TEST(test_whitespace_around);
    RUN_TEST(test_garbage_returns_null);
    TEST_RETURN_FAILED();
}
