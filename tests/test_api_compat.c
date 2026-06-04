#include "test.h"
#include "api_compat.h"
#include "conversation.h"

static int test_build_empty(void) {
    cJSON *arr = compat_build_messages(NULL, 0, NULL);
    ASSERT_NOT_NULL(arr);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 0);
    cJSON_Delete(arr);
    return 0;
}

static int test_build_sys(void) {
    cJSON *arr = compat_build_messages(NULL, 0, "You are helpful.");
    ASSERT_NOT_NULL(arr);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 1);
    cJSON *first = cJSON_GetArrayItem(arr, 0);
    ASSERT_NOT_NULL(first);
    const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(first, "role"));
    ASSERT_STR_EQ(role, "system");
    cJSON_Delete(arr);
    return 0;
}

static int test_build_user_msg(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "hello");
    cJSON *arr = compat_build_messages(conv.msgs, conv.len, NULL);
    ASSERT_NOT_NULL(arr);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 1);
    cJSON *m = cJSON_GetArrayItem(arr, 0);
    const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(m, "role"));
    ASSERT_STR_EQ(role, "user");
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(m, "content"));
    ASSERT_STR_EQ(content, "hello");
    cJSON_Delete(arr);
    conversation_free(&conv);
    return 0;
}

static int test_dev_role_toggle(void) {
    compat_set_developer_role(true);
    cJSON *arr = compat_build_messages(NULL, 0, "sys");
    cJSON *first = cJSON_GetArrayItem(arr, 0);
    const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(first, "role"));
    ASSERT_STR_EQ(role, "developer");
    cJSON_Delete(arr);

    compat_set_developer_role(false);
    arr = compat_build_messages(NULL, 0, "sys");
    first = cJSON_GetArrayItem(arr, 0);
    role = cJSON_GetStringValue(cJSON_GetObjectItem(first, "role"));
    ASSERT_STR_EQ(role, "system");
    cJSON_Delete(arr);
    return 0;
}

static int test_build_with_sys_and_user(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "question");
    cJSON *arr = compat_build_messages(conv.msgs, conv.len, "system prompt");
    ASSERT_NOT_NULL(arr);
    ASSERT_INT_EQ(cJSON_GetArraySize(arr), 2);
    cJSON_Delete(arr);
    conversation_free(&conv);
    return 0;
}

int test_api_compat_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("api_compat");
    RUN_TEST(test_build_empty);
    RUN_TEST(test_build_sys);
    RUN_TEST(test_build_user_msg);
    RUN_TEST(test_dev_role_toggle);
    RUN_TEST(test_build_with_sys_and_user);
    TEST_RETURN_FAILED();
}
