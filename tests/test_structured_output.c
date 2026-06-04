#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"
#include <string.h>

static int test_extract_direct_json(void) {
    char *out = structured_output_extract_json("{\"key\": \"value\"}");
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "key") != NULL);
    ASSERT(strstr(out, "value") != NULL);
    free(out);
    return 0;
}

static int test_extract_from_fence(void) {
    const char *input = "Here's the result:\n```json\n{\"x\": 1}\n```\nDone.";
    char *out = structured_output_extract_json(input);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "\"x\"") != NULL);
    free(out);
    return 0;
}

static int test_extract_embedded(void) {
    const char *input = "The answer is {\"result\": 42} as shown.";
    char *out = structured_output_extract_json(input);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "42") != NULL);
    free(out);
    return 0;
}

static int test_extract_null(void) {
    ASSERT_NULL(structured_output_extract_json(NULL));
    ASSERT_NULL(structured_output_extract_json("no json here at all"));
    return 0;
}

static int test_validate_no_schema(void) {
    ASSERT(structured_output_validate("{\"a\":1}", NULL));
    ASSERT(!structured_output_validate("not json", NULL));
    ASSERT(!structured_output_validate(NULL, NULL));
    return 0;
}

static int test_validate_required_keys(void) {
    const char *schema_str =
        "{\"type\":\"object\",\"required\":[\"name\",\"age\"],"
        "\"properties\":{\"name\":{\"type\":\"string\"},\"age\":{\"type\":\"number\"}}}";
    cJSON *schema = cJSON_Parse(schema_str);
    ASSERT_NOT_NULL(schema);

    ASSERT(structured_output_validate("{\"name\":\"Alice\",\"age\":30}", schema));
    ASSERT(!structured_output_validate("{\"name\":\"Bob\"}", schema));
    ASSERT(!structured_output_validate("{\"age\":25}", schema));

    cJSON_Delete(schema);
    return 0;
}

static int test_validate_type_check(void) {
    const char *schema_str = "{\"type\":\"object\",\"properties\":{"
                             "\"count\":{\"type\":\"number\"},"
                             "\"name\":{\"type\":\"string\"}}}";
    cJSON *schema = cJSON_Parse(schema_str);

    ASSERT(structured_output_validate("{\"count\":5,\"name\":\"x\"}", schema));
    ASSERT(!structured_output_validate("{\"count\":\"five\"}", schema));

    cJSON_Delete(schema);
    return 0;
}

static int test_inject_prompt(void) {
    char *out = structured_output_inject_prompt("You are helpful.", NULL);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "You are helpful") != NULL);
    ASSERT(strstr(out, "valid JSON") != NULL);
    free(out);
    return 0;
}

static int test_inject_prompt_with_schema(void) {
    cJSON *schema =
        cJSON_Parse("{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"}}}");
    char *out = structured_output_inject_prompt("System prompt.", schema);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "System prompt") != NULL);
    ASSERT(strstr(out, "schema") != NULL);
    free(out);
    cJSON_Delete(schema);
    return 0;
}

static int test_wrap_request_compat(void) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", "gpt-5.4");

    cJSON *wrapped = structured_output_wrap_request(body, NULL, PROVIDER_COMPAT);
    ASSERT_NOT_NULL(wrapped);
    cJSON *rf = cJSON_GetObjectItem(wrapped, "response_format");
    ASSERT_NOT_NULL(rf);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(rf, "type")), "json_object");

    cJSON_Delete(body);
    cJSON_Delete(wrapped);
    return 0;
}

static int test_wrap_request_with_schema(void) {
    cJSON *body = cJSON_CreateObject();
    cJSON *schema = cJSON_Parse("{\"type\":\"object\"}");

    cJSON *wrapped = structured_output_wrap_request(body, schema, PROVIDER_COMPAT);
    ASSERT_NOT_NULL(wrapped);
    cJSON *rf = cJSON_GetObjectItem(wrapped, "response_format");
    ASSERT_NOT_NULL(rf);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(rf, "type")), "json_schema");

    cJSON_Delete(body);
    cJSON_Delete(schema);
    cJSON_Delete(wrapped);
    return 0;
}

static int test_extract_array(void) {
    char *out = structured_output_extract_json("[1,2,3]");
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "1") != NULL);
    free(out);
    return 0;
}

int test_structured_output_all(void) {
    TEST_SUITE("structured_output");
    TEST_COUNTERS;

    RUN_TEST(test_extract_direct_json);
    RUN_TEST(test_extract_from_fence);
    RUN_TEST(test_extract_embedded);
    RUN_TEST(test_extract_null);
    RUN_TEST(test_validate_no_schema);
    RUN_TEST(test_validate_required_keys);
    RUN_TEST(test_validate_type_check);
    RUN_TEST(test_inject_prompt);
    RUN_TEST(test_inject_prompt_with_schema);
    RUN_TEST(test_wrap_request_compat);
    RUN_TEST(test_wrap_request_with_schema);
    RUN_TEST(test_extract_array);

    TEST_RETURN_FAILED();
}
