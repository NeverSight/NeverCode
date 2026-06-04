#include "test.h"
#include "nevercode.h"
#include "conversation.h"
#include <stdlib.h>
#include <string.h>

/* Uses declarations from nevercode.h */

static int test_init(void) {
    responses_api_init();
    ASSERT_FALSE(responses_api_enabled());
    ASSERT_INT_EQ(0, responses_api_request_count());
    return 0;
}

static int test_enable(void) {
    responses_api_init();
    responses_api_set_enabled(true);
    ASSERT_TRUE(responses_api_enabled());
    responses_api_set_enabled(false);
    ASSERT_FALSE(responses_api_enabled());
    return 0;
}

static int test_build_request(void) {
    responses_api_init();
    cJSON *req = responses_api_build_request(NULL, 0, "You are helpful", "gpt-5.4", NULL, 0, 8192);
    ASSERT_NOT_NULL(req);

    cJSON *model = cJSON_GetObjectItem(req, "model");
    ASSERT_NOT_NULL(model);

    cJSON *inst = cJSON_GetObjectItem(req, "instructions");
    ASSERT_NOT_NULL(inst);

    cJSON *input = cJSON_GetObjectItem(req, "input");
    ASSERT_NOT_NULL(input);
    ASSERT_INT_EQ(0, cJSON_GetArraySize(input));

    ASSERT_INT_EQ(1, responses_api_request_count());

    cJSON_Delete(req);
    return 0;
}

static int test_is_response_format(void) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "output", "[]");
    ASSERT_TRUE(responses_api_is_response_format(resp));
    cJSON_Delete(resp);

    cJSON *chat = cJSON_CreateObject();
    cJSON_AddStringToObject(chat, "choices", "[]");
    ASSERT_FALSE(responses_api_is_response_format(chat));
    cJSON_Delete(chat);
    return 0;
}

static int test_parse_output_text(void) {
    responses_api_init();
    const char *json = "{\"output\":[{\"type\":\"message\",\"role\":\"assistant\","
                       "\"content\":[{\"type\":\"output_text\",\"text\":\"Hello world\"}]}],"
                       "\"status\":\"completed\","
                       "\"usage\":{\"input_tokens\":10,\"output_tokens\":5}}";
    cJSON *body = cJSON_Parse(json);
    ASSERT_NOT_NULL(body);

    message_t msgs[4];
    int n = responses_api_parse_output(body, msgs, 4);
    ASSERT_INT_EQ(1, n);
    ASSERT_INT_EQ(ROLE_ASSISTANT, msgs[0].role);
    ASSERT_STR_EQ("Hello world", msgs[0].blocks[0].text);

    const char *reason = responses_api_finish_reason(body);
    ASSERT_STR_EQ("stop", reason);

    int in_tok = 0, out_tok = 0;
    ASSERT_INT_EQ(0, responses_api_usage(body, &in_tok, &out_tok));
    ASSERT_INT_EQ(10, in_tok);
    ASSERT_INT_EQ(5, out_tok);

    message_free(&msgs[0]);
    cJSON_Delete(body);
    return 0;
}

static int test_parse_output_tool_call(void) {
    responses_api_init();
    const char *json = "{\"output\":[{\"type\":\"function_call\",\"call_id\":\"c1\","
                       "\"name\":\"Read\",\"arguments\":\"{\\\"path\\\":\\\"/tmp/x\\\"}\"}],"
                       "\"status\":\"incomplete\"}";
    cJSON *body = cJSON_Parse(json);
    ASSERT_NOT_NULL(body);

    message_t msgs[4];
    int n = responses_api_parse_output(body, msgs, 4);
    ASSERT_INT_EQ(1, n);
    ASSERT_INT_EQ(ROLE_ASSISTANT, msgs[0].role);
    ASSERT_INT_EQ(CONTENT_TOOL_USE, msgs[0].blocks[0].type);
    ASSERT_STR_EQ("Read", msgs[0].blocks[0].tool_name);

    const char *reason = responses_api_finish_reason(body);
    ASSERT_STR_EQ("length", reason);

    message_free(&msgs[0]);
    cJSON_Delete(body);
    return 0;
}

static int test_parse_output_empty(void) {
    responses_api_init();
    ASSERT_INT_EQ(0, responses_api_parse_output(NULL, NULL, 0));

    cJSON *body = cJSON_Parse("{\"output\":[]}");
    message_t msgs[2];
    ASSERT_INT_EQ(0, responses_api_parse_output(body, msgs, 2));
    cJSON_Delete(body);
    return 0;
}

static int test_parse_output_malformed_array(void) {
    responses_api_init();
    ASSERT_INT_EQ(0, responses_api_error_count());
    cJSON *body = cJSON_Parse("{\"output\":\"not-an-array\"}");
    ASSERT_NOT_NULL(body);
    message_t msgs[2];
    ASSERT_INT_EQ(0, responses_api_parse_output(body, msgs, 2));
    ASSERT_INT_EQ(1, responses_api_error_count());
    cJSON_Delete(body);
    return 0;
}

static int test_format(void) {
    responses_api_init();
    char *fmt = responses_api_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "Responses API");
    free(fmt);
    return 0;
}

int test_responses_api_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("responses_api");
    RUN_TEST(test_init);
    RUN_TEST(test_enable);
    RUN_TEST(test_build_request);
    RUN_TEST(test_is_response_format);
    RUN_TEST(test_parse_output_text);
    RUN_TEST(test_parse_output_tool_call);
    RUN_TEST(test_parse_output_empty);
    RUN_TEST(test_parse_output_malformed_array);
    RUN_TEST(test_format);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("responses_api");
    RUN_TEST(test_init);
    RUN_TEST(test_enable);
    RUN_TEST(test_build_request);
    RUN_TEST(test_is_response_format);
    RUN_TEST(test_format);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
