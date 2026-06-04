#include "test.h"
#include "nevercode.h"
#include "conversation.h"
#include <unistd.h>
#include <stdio.h>

static int test_init(void) {
    conv_recovery_init();
    ASSERT(conv_recovery_enabled() == true);
    ASSERT_NOT_NULL(conv_recovery_path());
    return 0;
}

static int test_save_empty(void) {
    conv_recovery_init();
    conversation_t conv;
    conversation_init(&conv);
    int rc = conv_recovery_save(&conv, "test-sess");
    ASSERT(rc != 0);
    conversation_free(&conv);
    return 0;
}

static int test_save_load_roundtrip(void) {
    conv_recovery_init();
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "hello there");
    message_t *am = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(am, "hi back");

    int rc = conv_recovery_save(&conv, "roundtrip-001");
    ASSERT_INT_EQ(rc, 0);
    ASSERT(conv_recovery_exists() == true);

    conversation_t conv2;
    conversation_init(&conv2);
    char *sid = NULL;
    int loaded = conv_recovery_load(&conv2, &sid);
    ASSERT(loaded > 0);
    ASSERT_NOT_NULL(sid);
    ASSERT_STR_EQ(sid, "roundtrip-001");
    ASSERT(conv2.len >= 2);

    free(sid);
    conversation_free(&conv2);
    conversation_free(&conv);
    conv_recovery_clear();
    return 0;
}

static int test_clear(void) {
    conv_recovery_init();
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "test");
    conv_recovery_save(&conv, "clear-test");
    ASSERT(conv_recovery_exists() == true);

    conv_recovery_clear();
    ASSERT(conv_recovery_exists() == false);

    conversation_free(&conv);
    return 0;
}

static int test_describe_no_file(void) {
    conv_recovery_init();
    conv_recovery_clear();
    char *desc = conv_recovery_describe();
    ASSERT_NOT_NULL(desc);
    ASSERT(strstr(desc, "No recovery") != NULL);
    free(desc);
    return 0;
}

static int test_describe_with_file(void) {
    conv_recovery_init();
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "data");
    conv_recovery_save(&conv, "desc-test");

    char *desc = conv_recovery_describe();
    ASSERT_NOT_NULL(desc);
    ASSERT(strstr(desc, "Recovery file") != NULL);
    free(desc);

    conv_recovery_clear();
    conversation_free(&conv);
    return 0;
}

static int test_age(void) {
    conv_recovery_init();
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "age test");
    conv_recovery_save(&conv, "age-test");

    long age = conv_recovery_age_seconds();
    ASSERT(age >= 0);
    ASSERT(age < 5);

    conv_recovery_clear();
    conversation_free(&conv);
    return 0;
}

static int test_tool_roundtrip(void) {
    conv_recovery_init();
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "run test");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "ls");
    message_t *am2 = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_tool_use(am2, "tu-001", "Bash", input);
    cJSON_Delete(input);
    message_t *tm = conversation_add(&conv, ROLE_TOOL);
    message_add_tool_result(tm, "tu-001", "file1\nfile2\n", 0);

    int rc = conv_recovery_save(&conv, "tool-test");
    ASSERT_INT_EQ(rc, 0);

    conversation_t conv2;
    conversation_init(&conv2);
    int loaded = conv_recovery_load(&conv2, NULL);
    ASSERT(loaded > 0);
    ASSERT(conv2.len >= 3);

    conversation_free(&conv2);
    conversation_free(&conv);
    conv_recovery_clear();
    return 0;
}

int test_conv_recovery_all(void) {
    TEST_SUITE("conv_recovery");
    TEST_COUNTERS;

    RUN_TEST(test_init);
    RUN_TEST(test_save_empty);
    RUN_TEST(test_save_load_roundtrip);
    RUN_TEST(test_clear);
    RUN_TEST(test_describe_no_file);
    RUN_TEST(test_describe_with_file);
    RUN_TEST(test_age);
    RUN_TEST(test_tool_roundtrip);

    TEST_RETURN_FAILED();
}
