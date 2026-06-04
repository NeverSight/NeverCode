#include "test.h"
#include "nevercode.h"
#include "conversation.h"

static int test_estimate_empty(void) {
    ASSERT_INT_EQ(estimate_tokens(NULL), 0);
    ASSERT_INT_EQ(estimate_tokens(""), 0);
    return 0;
}

static int test_estimate_simple(void) {
    int t = estimate_tokens("hello world");
    ASSERT(t >= 2 && t <= 4);
    return 0;
}

static int test_estimate_code(void) {
    int t = estimate_tokens("int main(void) { return 0; }");
    ASSERT(t >= 5 && t <= 15);
    return 0;
}

static int test_estimate_long_text(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < 100; i++)
        strbuf_append_str(&sb, "The quick brown fox jumps over the lazy dog. ");
    int t = estimate_tokens(sb.data);
    ASSERT(t >= 500 && t <= 1500);
    strbuf_free(&sb);
    return 0;
}

static int test_estimate_punctuation(void) {
    int t = estimate_tokens("hello, world! how are you?");
    ASSERT(t >= 5 && t <= 12);
    return 0;
}

static int test_estimate_message(void) {
    message_t m;
    message_init(&m, ROLE_USER);
    message_add_text(&m, "Hello, how are you doing today?");
    int t = estimate_message_tokens(&m);
    ASSERT(t >= 8);
    message_free(&m);
    return 0;
}

static int test_estimate_tool_message(void) {
    message_t m;
    message_init(&m, ROLE_ASSISTANT);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "ls -la /tmp");
    message_add_tool_use(&m, "tc_1", "Bash", input);
    cJSON_Delete(input);

    int t = estimate_message_tokens(&m);
    ASSERT(t >= 10);
    message_free(&m);
    return 0;
}

static int test_estimate_conversation(void) {
    message_t msgs[3];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Hello");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "Hi there!");
    message_init(&msgs[2], ROLE_USER);
    message_add_text(&msgs[2], "How are you?");

    int t = estimate_conversation_tokens(msgs, 3, "You are a helpful assistant.");
    ASSERT(t >= 15);

    for (int i = 0; i < 3; i++)
        message_free(&msgs[i]);
    return 0;
}

static int test_estimate_with_system(void) {
    int t_no_sys = estimate_conversation_tokens(NULL, 0, NULL);
    int t_with_sys = estimate_conversation_tokens(NULL, 0, "You are a coding assistant.");
    ASSERT(t_with_sys > t_no_sys);
    return 0;
}

static int test_estimate_proportional(void) {
    int t_short = estimate_tokens("short");
    int t_long =
        estimate_tokens("This is a much longer sentence that should produce more tokens "
                        "than the short one above because it has more words and characters.");
    ASSERT(t_long > t_short);
    return 0;
}

static int test_estimate_cjk(void) {
    int t_cjk = estimate_tokens("\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c");
    int t_ascii = estimate_tokens("hi");
    ASSERT(t_cjk > t_ascii);
    return 0;
}

static int test_estimate_cjk_heavier_than_latin(void) {
    int t_cjk = estimate_tokens("\xe5\x85\xa8\xe9\x83\xa8");
    int t_lat = estimate_tokens("AB");
    ASSERT(t_cjk >= t_lat);
    return 0;
}

static int test_estimate_emoji(void) {
    int t = estimate_tokens("\xf0\x9f\x98\x80\xf0\x9f\x98\x82");
    ASSERT(t >= 2);
    return 0;
}

int test_token_estimate_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("token_estimate");
    RUN_TEST(test_estimate_empty);
    RUN_TEST(test_estimate_simple);
    RUN_TEST(test_estimate_code);
    RUN_TEST(test_estimate_long_text);
    RUN_TEST(test_estimate_punctuation);
    RUN_TEST(test_estimate_message);
    RUN_TEST(test_estimate_tool_message);
    RUN_TEST(test_estimate_conversation);
    RUN_TEST(test_estimate_with_system);
    RUN_TEST(test_estimate_proportional);
    RUN_TEST(test_estimate_cjk);
    RUN_TEST(test_estimate_cjk_heavier_than_latin);
    RUN_TEST(test_estimate_emoji);
    TEST_RETURN_FAILED();
}
