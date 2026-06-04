#include "test.h"
#include "nevercode.h"
#include "conversation.h"

static int test_conv_init(void) {
    conversation_t c;
    conversation_init(&c);
    ASSERT_INT_EQ(c.len, 0);
    ASSERT_INT_EQ(c.cap, 0);
    ASSERT_NULL(c.msgs);
    ASSERT_NULL(c.system_prompt);
    conversation_free(&c);
    return 0;
}

static int test_conv_add(void) {
    conversation_t c;
    conversation_init(&c);
    message_t *m = conversation_add(&c, ROLE_USER);
    ASSERT_NOT_NULL(m);
    ASSERT_INT_EQ(c.len, 1);
    ASSERT_INT_EQ(m->role, ROLE_USER);
    conversation_free(&c);
    return 0;
}

static int test_conv_add_user_text(void) {
    conversation_t c;
    conversation_init(&c);
    conversation_add_user_text(&c, "hello world");
    ASSERT_INT_EQ(c.len, 1);
    ASSERT_INT_EQ(c.msgs[0].role, ROLE_USER);
    ASSERT_INT_EQ(c.msgs[0].num_blocks, 1);
    ASSERT_INT_EQ(c.msgs[0].blocks[0].type, CONTENT_TEXT);
    ASSERT_STR_EQ(c.msgs[0].blocks[0].text, "hello world");
    conversation_free(&c);
    return 0;
}

static int test_conv_multiple_messages(void) {
    conversation_t c;
    conversation_init(&c);
    conversation_add_user_text(&c, "user msg");

    message_t *am = conversation_add(&c, ROLE_ASSISTANT);
    message_add_text(am, "assistant reply");

    conversation_add_user_text(&c, "user follow up");
    ASSERT_INT_EQ(c.len, 3);
    ASSERT_INT_EQ(c.msgs[0].role, ROLE_USER);
    ASSERT_INT_EQ(c.msgs[1].role, ROLE_ASSISTANT);
    ASSERT_INT_EQ(c.msgs[2].role, ROLE_USER);
    conversation_free(&c);
    return 0;
}

static int test_msg_text(void) {
    message_t m;
    message_init(&m, ROLE_USER);
    message_add_text(&m, "text content");
    ASSERT_INT_EQ(m.num_blocks, 1);
    ASSERT_INT_EQ(m.blocks[0].type, CONTENT_TEXT);
    ASSERT_STR_EQ(m.blocks[0].text, "text content");
    message_free(&m);
    return 0;
}

static int test_msg_tool_use(void) {
    message_t m;
    message_init(&m, ROLE_ASSISTANT);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "ls -la");
    message_add_tool_use(&m, "call_123", "Bash", input);
    cJSON_Delete(input);

    ASSERT_INT_EQ(m.num_blocks, 1);
    ASSERT_INT_EQ(m.blocks[0].type, CONTENT_TOOL_USE);
    ASSERT_STR_EQ(m.blocks[0].tool_use_id, "call_123");
    ASSERT_STR_EQ(m.blocks[0].tool_name, "Bash");
    ASSERT_NOT_NULL(m.blocks[0].tool_input);
    const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(m.blocks[0].tool_input, "command"));
    ASSERT_STR_EQ(cmd, "ls -la");
    message_free(&m);
    return 0;
}

static int test_msg_tool_result(void) {
    message_t m;
    message_init(&m, ROLE_TOOL);
    message_add_tool_result(&m, "call_123", "file.txt\ndata.csv", 0);
    ASSERT_INT_EQ(m.num_blocks, 1);
    ASSERT_INT_EQ(m.blocks[0].type, CONTENT_TOOL_RESULT);
    ASSERT_STR_EQ(m.blocks[0].tool_use_id, "call_123");
    ASSERT_STR_EQ(m.blocks[0].text, "file.txt\ndata.csv");
    ASSERT_INT_EQ(m.blocks[0].is_error, 0);
    message_free(&m);
    return 0;
}

static int test_msg_tool_result_error(void) {
    message_t m;
    message_init(&m, ROLE_TOOL);
    message_add_tool_result(&m, "call_456", "Permission denied", 1);
    ASSERT_INT_EQ(m.blocks[0].is_error, 1);
    message_free(&m);
    return 0;
}

static int test_msg_multi_blocks(void) {
    message_t m;
    message_init(&m, ROLE_ASSISTANT);
    message_add_text(&m, "I'll run the command:");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "echo hi");
    message_add_tool_use(&m, "tc_1", "Bash", input);
    cJSON_Delete(input);

    ASSERT_INT_EQ(m.num_blocks, 2);
    ASSERT_INT_EQ(m.blocks[0].type, CONTENT_TEXT);
    ASSERT_INT_EQ(m.blocks[1].type, CONTENT_TOOL_USE);
    message_free(&m);
    return 0;
}

static int test_conv_growth(void) {
    conversation_t c;
    conversation_init(&c);
    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "msg %d", i);
        conversation_add_user_text(&c, buf);
    }
    ASSERT_INT_EQ(c.len, 100);
    ASSERT(c.cap >= 100);
    ASSERT_STR_EQ(c.msgs[0].blocks[0].text, "msg 0");
    ASSERT_STR_EQ(c.msgs[99].blocks[0].text, "msg 99");
    conversation_free(&c);
    return 0;
}

static int test_conv_capacity_macros(void) {
    ASSERT(CONV_INIT_CAP > 0);
    ASSERT(CONV_GROW_FACTOR >= 2);
    ASSERT(MSG_BLOCK_INIT_CAP > 0);
    ASSERT(MSG_BLOCK_GROW_FACTOR >= 2);

    conversation_t c;
    conversation_init(&c);
    conversation_add_user_text(&c, "first");
    ASSERT(c.cap >= CONV_INIT_CAP);

    message_t m;
    message_init(&m, ROLE_USER);
    message_add_text(&m, "a");
    ASSERT(m.cap_blocks >= MSG_BLOCK_INIT_CAP);
    message_free(&m);
    conversation_free(&c);
    return 0;
}

static int test_conv_remove_at(void) {
    conversation_t c;
    conversation_init(&c);
    conversation_add_user_text(&c, "zero");
    conversation_add_user_text(&c, "one");
    conversation_add_user_text(&c, "two");
    ASSERT_INT_EQ(c.len, 3);

    conversation_remove_at(&c, 1);
    ASSERT_INT_EQ(c.len, 2);
    ASSERT_STR_EQ(c.msgs[0].blocks[0].text, "zero");
    ASSERT_STR_EQ(c.msgs[1].blocks[0].text, "two");

    conversation_remove_at(&c, 0);
    ASSERT_INT_EQ(c.len, 1);
    ASSERT_STR_EQ(c.msgs[0].blocks[0].text, "two");

    conversation_remove_at(&c, 5);
    ASSERT_INT_EQ(c.len, 1);

    conversation_free(&c);
    return 0;
}

static int test_conv_insert_at(void) {
    conversation_t c;
    conversation_init(&c);
    conversation_add_user_text(&c, "first");
    conversation_add_user_text(&c, "third");

    message_t *m = conversation_insert_at(&c, 1, ROLE_USER);
    message_add_text(m, "second");
    ASSERT_INT_EQ(c.len, 3);
    ASSERT_STR_EQ(c.msgs[0].blocks[0].text, "first");
    ASSERT_STR_EQ(c.msgs[1].blocks[0].text, "second");
    ASSERT_STR_EQ(c.msgs[2].blocks[0].text, "third");

    message_t *head = conversation_insert_at(&c, 0, ROLE_USER);
    message_add_text(head, "head");
    ASSERT_INT_EQ(c.len, 4);
    ASSERT_STR_EQ(c.msgs[0].blocks[0].text, "head");

    conversation_free(&c);
    return 0;
}

static int test_conv_drop_range(void) {
    conversation_t c;
    conversation_init(&c);
    for (int i = 0; i < 6; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "m%d", i);
        conversation_add_user_text(&c, buf);
    }
    ASSERT_INT_EQ(c.len, 6);

    int dropped = conversation_drop_range(&c, 1, 3);
    ASSERT_INT_EQ(dropped, 3);
    ASSERT_INT_EQ(c.len, 3);
    ASSERT_STR_EQ(c.msgs[0].blocks[0].text, "m0");
    ASSERT_STR_EQ(c.msgs[1].blocks[0].text, "m4");
    ASSERT_STR_EQ(c.msgs[2].blocks[0].text, "m5");

    conversation_free(&c);
    return 0;
}

static int test_conv_replace_head(void) {
    conversation_t c;
    conversation_init(&c);
    for (int i = 0; i < 5; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "msg%d", i);
        conversation_add_user_text(&c, buf);
    }
    ASSERT_INT_EQ(c.len, 5);

    int newlen = conversation_replace_head(&c, 3, "summary of first 3 messages");
    ASSERT_INT_EQ(newlen, 3);
    ASSERT_INT_EQ(c.len, 3);
    ASSERT_STR_EQ(c.msgs[0].blocks[0].text, "summary of first 3 messages");
    ASSERT_STR_EQ(c.msgs[1].blocks[0].text, "msg3");
    ASSERT_STR_EQ(c.msgs[2].blocks[0].text, "msg4");

    conversation_free(&c);
    return 0;
}

static int test_conv_replace_head_default_summary_macro(void) {
    conversation_t c;
    conversation_init(&c);
    conversation_add_user_text(&c, "msg0");
    conversation_add_user_text(&c, "msg1");
    ASSERT_INT_EQ(c.len, 2);

    int newlen = conversation_replace_head(&c, 1, NULL);
    ASSERT_INT_EQ(newlen, 2);
    ASSERT_STR_EQ(c.msgs[0].blocks[0].text, CONV_COMPACTED_FALLBACK_TEXT);
    ASSERT_STR_EQ(c.msgs[1].blocks[0].text, "msg1");

    conversation_free(&c);
    return 0;
}

int test_conversation_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("conversation");
    RUN_TEST(test_conv_init);
    RUN_TEST(test_conv_add);
    RUN_TEST(test_conv_add_user_text);
    RUN_TEST(test_conv_multiple_messages);
    RUN_TEST(test_msg_text);
    RUN_TEST(test_msg_tool_use);
    RUN_TEST(test_msg_tool_result);
    RUN_TEST(test_msg_tool_result_error);
    RUN_TEST(test_msg_multi_blocks);
    RUN_TEST(test_conv_growth);
    RUN_TEST(test_conv_capacity_macros);
    RUN_TEST(test_conv_remove_at);
    RUN_TEST(test_conv_insert_at);
    RUN_TEST(test_conv_drop_range);
    RUN_TEST(test_conv_replace_head);
    RUN_TEST(test_conv_replace_head_default_summary_macro);
    TEST_RETURN_FAILED();
}
