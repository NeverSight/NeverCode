#include "test.h"
#include "nevercode.h"
#include "api_compat.h"
#include "api_messages.h"
#include "conversation.h"

static int test_compat_simple_message(void) {
    message_t msgs[1];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Hello");

    cJSON *arr = compat_build_messages(msgs, 1, "You are helpful.");
    ASSERT_NOT_NULL(arr);

    int n = cJSON_GetArraySize(arr);
    ASSERT_INT_EQ(n, 2);

    cJSON *sys = cJSON_GetArrayItem(arr, 0);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(sys, "role")), "system");
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(sys, "content")), "You are helpful.");

    cJSON *user = cJSON_GetArrayItem(arr, 1);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(user, "role")), "user");
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(user, "content")), "Hello");

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    return 0;
}

static int test_compat_no_system(void) {
    message_t msgs[1];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Hi");

    cJSON *arr = compat_build_messages(msgs, 1, NULL);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 1);

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    return 0;
}

static int test_compat_tool_call(void) {
    message_t msgs[2];
    message_init(&msgs[0], ROLE_ASSISTANT);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "ls");
    message_add_tool_use(&msgs[0], "tc_1", "Bash", input);
    cJSON_Delete(input);

    message_init(&msgs[1], ROLE_TOOL);
    message_add_tool_result(&msgs[1], "tc_1", "file.txt", 0);

    cJSON *arr = compat_build_messages(msgs, 2, NULL);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 2);

    cJSON *asst = cJSON_GetArrayItem(arr, 0);
    cJSON *tool_calls = cJSON_GetObjectItem(asst, "tool_calls");
    ASSERT_NOT_NULL(tool_calls);
    ASSERT_INT_EQ(cJSON_GetArraySize(tool_calls), 1);

    cJSON *tc = cJSON_GetArrayItem(tool_calls, 0);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(tc, "id")), "tc_1");
    cJSON *fn = cJSON_GetObjectItem(tc, "function");
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(fn, "name")), "Bash");

    cJSON *tool_msg = cJSON_GetArrayItem(arr, 1);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(tool_msg, "role")), "tool");
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(tool_msg, "tool_call_id")), "tc_1");

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    message_free(&msgs[1]);
    return 0;
}

static int test_compat_multi_turn(void) {
    message_t msgs[3];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Hello");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "Hi there!");
    message_init(&msgs[2], ROLE_USER);
    message_add_text(&msgs[2], "How are you?");

    cJSON *arr = compat_build_messages(msgs, 3, "Be friendly.");
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 4);

    cJSON_Delete(arr);
    for (int i = 0; i < 3; i++)
        message_free(&msgs[i]);
    return 0;
}

static int test_messages_payload_simple(void) {
    message_t msgs[1];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Hello");

    cJSON *arr = messages_build_payload(msgs, 1);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 1);

    cJSON *m = cJSON_GetArrayItem(arr, 0);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(m, "role")), "user");

    /* Content may be a string or array (array when cache breakpoints applied) */
    cJSON *content = cJSON_GetObjectItem(m, "content");
    ASSERT_NOT_NULL(content);
    if (cJSON_IsString(content)) {
        ASSERT_STR_EQ(cJSON_GetStringValue(content), "Hello");
    } else {
        ASSERT(cJSON_IsArray(content));
        cJSON *first = cJSON_GetArrayItem(content, 0);
        ASSERT_NOT_NULL(first);
        ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(first, "text")), "Hello");
    }

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    return 0;
}

static int test_messages_payload_tool_use(void) {
    message_t msgs[2];
    message_init(&msgs[0], ROLE_ASSISTANT);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", "/tmp/test.txt");
    message_add_tool_use(&msgs[0], "tu_1", "Read", input);
    cJSON_Delete(input);

    message_init(&msgs[1], ROLE_TOOL);
    message_add_tool_result(&msgs[1], "tu_1", "file contents here", 0);

    cJSON *arr = messages_build_payload(msgs, 2);
    ASSERT_NOT_NULL(arr);
    ASSERT(cJSON_GetArraySize(arr) >= 1);

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    message_free(&msgs[1]);
    return 0;
}

static int test_messages_payload_alternation(void) {
    message_t msgs[4];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Hi");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "Hello");
    message_init(&msgs[2], ROLE_USER);
    message_add_text(&msgs[2], "More");
    message_init(&msgs[3], ROLE_ASSISTANT);
    message_add_text(&msgs[3], "Sure");

    cJSON *arr = messages_build_payload(msgs, 4);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 4);

    for (int i = 0; i < 4; i++) {
        cJSON *m = cJSON_GetArrayItem(arr, i);
        const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(m, "role"));
        ASSERT_STR_EQ(role, (i % 2 == 0) ? "user" : "assistant");
    }

    cJSON_Delete(arr);
    for (int i = 0; i < 4; i++)
        message_free(&msgs[i]);
    return 0;
}

static int test_messages_payload_merge_consecutive(void) {
    /* tool results (ROLE_TOOL) should merge with previous tool result under "user" */
    message_t msgs[3];
    message_init(&msgs[0], ROLE_ASSISTANT);
    cJSON *in1 = cJSON_CreateObject();
    cJSON_AddStringToObject(in1, "command", "ls");
    message_add_tool_use(&msgs[0], "tc1", "Bash", in1);
    cJSON_Delete(in1);
    cJSON *in2 = cJSON_CreateObject();
    cJSON_AddStringToObject(in2, "path", "/tmp");
    message_add_tool_use(&msgs[0], "tc2", "Read", in2);
    cJSON_Delete(in2);

    message_init(&msgs[1], ROLE_TOOL);
    message_add_tool_result(&msgs[1], "tc1", "result1", 0);
    message_init(&msgs[2], ROLE_TOOL);
    message_add_tool_result(&msgs[2], "tc2", "result2", 0);

    cJSON *arr = messages_build_payload(msgs, 3);
    ASSERT_NOT_NULL(arr);
    /* Should merge the two tool messages into one "user" message */
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 2);

    cJSON_Delete(arr);
    for (int i = 0; i < 3; i++)
        message_free(&msgs[i]);
    return 0;
}

static int test_compat_developer_role(void) {
    compat_set_developer_role(true);

    message_t msgs[1];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Hello");

    cJSON *arr = compat_build_messages(msgs, 1, "You are helpful.");
    ASSERT_NOT_NULL(arr);

    cJSON *sys = cJSON_GetArrayItem(arr, 0);
    ASSERT_STR_EQ(cJSON_GetStringValue(cJSON_GetObjectItem(sys, "role")), "developer");

    cJSON_Delete(arr);
    message_free(&msgs[0]);
    compat_set_developer_role(false);
    return 0;
}

int test_api_messages_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("api_messages");
    RUN_TEST(test_compat_simple_message);
    RUN_TEST(test_compat_no_system);
    RUN_TEST(test_compat_tool_call);
    RUN_TEST(test_compat_multi_turn);
    RUN_TEST(test_compat_developer_role);
    RUN_TEST(test_messages_payload_simple);
    RUN_TEST(test_messages_payload_tool_use);
    RUN_TEST(test_messages_payload_alternation);
    RUN_TEST(test_messages_payload_merge_consecutive);
    TEST_RETURN_FAILED();
}
