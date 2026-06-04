#include "test.h"
#include "nevercode.h"
#include "conversation.h"

static int test_truncate_rounds_empty(void) {
    ASSERT_INT_EQ(thread_truncate_rounds(NULL, 0, 1), 0);
    return 0;
}

static int test_truncate_rounds_basic(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *m;

    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Hello");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Hi there");
    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Next question");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Answer");

    int cut = thread_truncate_rounds(conv.msgs, conv.len, 1);
    ASSERT(cut > 0);
    ASSERT(cut <= conv.len);

    conversation_free(&conv);
    return 0;
}

static int test_safe_cut(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *m;

    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Do something");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "OK");
    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "More");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Done");

    int safe = thread_find_safe_cut(conv.msgs, conv.len, 2);
    ASSERT(safe >= 0);
    ASSERT(safe <= conv.len);

    conversation_free(&conv);
    return 0;
}

static int test_truncate_safe(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *m;

    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Q1");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "A1");
    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Q2");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "A2");
    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Q3");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "A3");

    int cut = thread_truncate_safe(conv.msgs, conv.len, 1);
    ASSERT(cut >= 0);

    conversation_free(&conv);
    return 0;
}

static int test_describe(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *m;

    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Test");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Response");

    char *desc = thread_truncation_describe(conv.msgs, conv.len);
    ASSERT_NOT_NULL(desc);
    ASSERT(strstr(desc, "Conversation") != NULL);
    ASSERT(strstr(desc, "messages") != NULL);
    free(desc);

    conversation_free(&conv);
    return 0;
}

static int test_describe_empty(void) {
    char *desc = thread_truncation_describe(NULL, 0);
    ASSERT_NOT_NULL(desc);
    ASSERT(strstr(desc, "empty") != NULL);
    free(desc);
    return 0;
}

static int test_token_budget(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *m;

    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Question one with some content");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Answer one with some content");
    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Question two");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Answer two");

    int cut = thread_truncate_to_token_budget(conv.msgs, conv.len, NULL, 10);
    ASSERT(cut >= 0);

    conversation_free(&conv);
    return 0;
}

static int test_zero_rounds(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "test");
    ASSERT_INT_EQ(thread_truncate_rounds(conv.msgs, conv.len, 0), 0);
    conversation_free(&conv);
    return 0;
}

static int test_safe_cut_consecutive_assistant(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *m;

    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Do the task");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Thinking about it...");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Here is the plan");
    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Go ahead");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Done");

    /* Desired cut at index 2 (second assistant) should pull back to
     * index 1 (first assistant) so the consecutive pair stays together. */
    int safe = thread_find_safe_cut(conv.msgs, conv.len, 2);
    ASSERT(safe <= 1);

    conversation_free(&conv);
    return 0;
}

static int test_safe_cut_no_orphan(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *m;

    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Start");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_tool_use(m, "tc1", "read", NULL);
    m = conversation_add(&conv, ROLE_TOOL);
    message_add_tool_result(m, "tc1", "file content", 0);
    m = conversation_add(&conv, ROLE_USER);
    message_add_text(m, "Next");
    m = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(m, "Done");

    /* Cutting at index 2 (tool result) would orphan it from its
     * matching tool_use at index 1.  Safe cut should move forward. */
    int safe = thread_find_safe_cut(conv.msgs, conv.len, 2);
    ASSERT(safe >= 3);

    conversation_free(&conv);
    return 0;
}

int test_thread_truncation_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("thread_truncation");
    RUN_TEST(test_truncate_rounds_empty);
    RUN_TEST(test_truncate_rounds_basic);
    RUN_TEST(test_safe_cut);
    RUN_TEST(test_truncate_safe);
    RUN_TEST(test_describe);
    RUN_TEST(test_describe_empty);
    RUN_TEST(test_token_budget);
    RUN_TEST(test_zero_rounds);
    RUN_TEST(test_safe_cut_consecutive_assistant);
    RUN_TEST(test_safe_cut_no_orphan);
    TEST_RETURN_FAILED();
}
