#include "test.h"
#include "nevercode.h"
#include "api_compat.h"
#include "api_messages.h"
#include "conversation.h"

static int test_compat_assistant_text_and_tool(void) {
    message_t msgs[1];
    message_init(&msgs[0], ROLE_ASSISTANT);
    message_add_text(&msgs[0], "Let me check that for you.");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", "/tmp/file.txt");
    message_add_tool_use(&msgs[0], "call_abc", "Read", input);
    cJSON_Delete(input);

    cJSON *arr = compat_build_messages(msgs, 1, NULL);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 1);

    cJSON *m = cJSON_GetArrayItem(arr, 0);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(m, "role")), "assistant");

    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(m, "content"));
    ASSERT_STR_EQ(content, "Let me check that for you.");

    cJSON *tc = cJSON_GetObjectItem(m, "tool_calls");
    ASSERT_NOT_NULL(tc);
    ASSERT_INT_EQ(cJSON_GetArraySize(tc), 1);

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    return 0;
}

static int test_compat_assistant_tool_only(void) {
    message_t msgs[1];
    message_init(&msgs[0], ROLE_ASSISTANT);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "ls");
    message_add_tool_use(&msgs[0], "call_xyz", "Bash", input);
    cJSON_Delete(input);

    cJSON *arr = compat_build_messages(msgs, 1, NULL);
    cJSON *m = cJSON_GetArrayItem(arr, 0);

    cJSON *content = cJSON_GetObjectItem(m, "content");
    ASSERT(cJSON_IsNull(content));

    cJSON *tc = cJSON_GetObjectItem(m, "tool_calls");
    ASSERT_NOT_NULL(tc);

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    return 0;
}

static int test_compat_multiple_tool_results(void) {
    message_t msgs[1];
    message_init(&msgs[0], ROLE_TOOL);
    message_add_tool_result(&msgs[0], "tc_a", "result A", 0);
    message_add_tool_result(&msgs[0], "tc_b", "result B", 1);

    cJSON *arr = compat_build_messages(msgs, 1, NULL);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 2);

    cJSON *m0 = cJSON_GetArrayItem(arr, 0);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(m0, "role")), "tool");
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(m0, "tool_call_id")), "tc_a");

    cJSON *m1 = cJSON_GetArrayItem(arr, 1);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(m1, "tool_call_id")), "tc_b");

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    return 0;
}

static int test_messages_payload_tool_result_error(void) {
    message_t msgs[2];
    message_init(&msgs[0], ROLE_ASSISTANT);
    cJSON *inp = cJSON_CreateObject();
    cJSON_AddStringToObject(inp, "command", "rm -rf /");
    message_add_tool_use(&msgs[0], "t1", "Bash", inp);
    cJSON_Delete(inp);

    message_init(&msgs[1], ROLE_TOOL);
    message_add_tool_result(&msgs[1], "t1", "Permission denied", 1);

    cJSON *arr = messages_build_payload(msgs, 2);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 2);

    cJSON *user_msg = cJSON_GetArrayItem(arr, 1);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(user_msg, "role")), "user");

    cJSON *content = cJSON_GetObjectItem(user_msg, "content");
    ASSERT(cJSON_IsArray(content));
    cJSON *block = cJSON_GetArrayItem(content, 0);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(block, "type")), "tool_result");
    ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(block, "is_error")));

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    message_free(&msgs[1]);
    return 0;
}

static int test_compat_empty_conversation(void) {
    cJSON *arr = compat_build_messages(NULL, 0, "system");
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 1);
    cJSON *sys = cJSON_GetArrayItem(arr, 0);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(sys, "role")), "system");
    cJSON_Delete(arr);
    return 0;
}

static int test_messages_payload_empty(void) {
    cJSON *arr = messages_build_payload(NULL, 0);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 0);
    cJSON_Delete(arr);
    return 0;
}

static int test_compat_long_conversation(void) {
    message_t msgs[20];
    for (int i = 0; i < 20; i++) {
        message_init(&msgs[i], (i % 2 == 0) ? ROLE_USER : ROLE_ASSISTANT);
        char buf[32];
        snprintf(buf, sizeof(buf), "Message %d", i);
        message_add_text(&msgs[i], buf);
    }

    cJSON *arr = compat_build_messages(msgs, 20, "sys");
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 21);

    cJSON_Delete(arr);
    for (int i = 0; i < 20; i++)
        message_free(&msgs[i]);
    return 0;
}

static int test_messages_payload_mixed_tool_text(void) {
    message_t msgs[3];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Hello");

    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "Let me help.");
    cJSON *inp = cJSON_CreateObject();
    cJSON_AddStringToObject(inp, "path", "/test");
    message_add_tool_use(&msgs[1], "tu1", "Read", inp);
    cJSON_Delete(inp);

    message_init(&msgs[2], ROLE_TOOL);
    message_add_tool_result(&msgs[2], "tu1", "contents", 0);

    cJSON *arr = messages_build_payload(msgs, 3);
    ASSERT_NOT_NULL(arr);
    ASSERT(cJSON_GetArraySize(arr) >= 2);

    cJSON *asst = cJSON_GetArrayItem(arr, 1);
    cJSON *content = cJSON_GetObjectItem(asst, "content");
    ASSERT(cJSON_IsArray(content));
    ASSERT(cJSON_GetArraySize(content) >= 2);

    cJSON_Delete(arr);
    for (int i = 0; i < 3; i++)
        message_free(&msgs[i]);
    return 0;
}

int test_sse_parsing_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("sse_parsing");
    RUN_TEST(test_compat_assistant_text_and_tool);
    RUN_TEST(test_compat_assistant_tool_only);
    RUN_TEST(test_compat_multiple_tool_results);
    RUN_TEST(test_messages_payload_tool_result_error);
    RUN_TEST(test_compat_empty_conversation);
    RUN_TEST(test_messages_payload_empty);
    RUN_TEST(test_compat_long_conversation);
    RUN_TEST(test_messages_payload_mixed_tool_text);
    TEST_RETURN_FAILED();
}
