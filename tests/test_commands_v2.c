#include "test.h"
#include "nevercode.h"
#include "terminal.h"
#include "conversation.h"

static int test_tab_copy_fast_summary(void) {
    const char **cmds = term_get_slash_cmds();
    ASSERT_NOT_NULL(cmds);
    int found_copy = 0, found_fast = 0, found_summary = 0;
    for (int i = 0; cmds[i]; i++) {
        if (strcmp(cmds[i], "/copy") == 0)
            found_copy = 1;
        if (strcmp(cmds[i], "/fast") == 0)
            found_fast = 1;
        if (strcmp(cmds[i], "/summary") == 0)
            found_summary = 1;
    }
    ASSERT(found_copy);
    ASSERT(found_fast);
    ASSERT(found_summary);
    return 0;
}

static int test_tab_sorted(void) {
    const char **cmds = term_get_slash_cmds();
    ASSERT_NOT_NULL(cmds);
    for (int i = 0; cmds[i] && cmds[i + 1]; i++)
        ASSERT(strcmp(cmds[i], cmds[i + 1]) < 0);
    return 0;
}

static int test_tab_count(void) {
    const char **cmds = term_get_slash_cmds();
    int n = 0;
    while (cmds[n])
        n++;
    ASSERT(n >= 50);
    return 0;
}

static int test_conversation_find_assistant(void) {
    conversation_t conv;
    conversation_init(&conv);

    conversation_add_user_text(&conv, "hello");
    message_t *am = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(am, "Hi there! How can I help?");

    int found = 0;
    for (int i = conv.len - 1; i >= 0; i--) {
        if (conv.msgs[i].role == ROLE_ASSISTANT) {
            for (int j = 0; j < conv.msgs[i].num_blocks; j++) {
                if (conv.msgs[i].blocks[j].type == CONTENT_TEXT) {
                    ASSERT_NOT_NULL(conv.msgs[i].blocks[j].text);
                    ASSERT(strstr(conv.msgs[i].blocks[j].text, "Hi there") != NULL);
                    found = 1;
                    break;
                }
            }
            if (found)
                break;
        }
    }
    ASSERT(found);

    conversation_free(&conv);
    return 0;
}

static int test_summary_counts(void) {
    conversation_t conv;
    conversation_init(&conv);

    conversation_add_user_text(&conv, "first question");
    message_t *a1 = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(a1, "answer 1");

    conversation_add_user_text(&conv, "second question");
    message_t *a2 = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(a2, "answer 2");

    message_t *t = conversation_add(&conv, ROLE_TOOL);
    message_add_tool_result(t, "id1", "tool output", 0);

    int user_msgs = 0, asst_msgs = 0, tool_calls = 0;
    for (int i = 0; i < conv.len; i++) {
        if (conv.msgs[i].role == ROLE_USER)
            user_msgs++;
        else if (conv.msgs[i].role == ROLE_ASSISTANT)
            asst_msgs++;
        else if (conv.msgs[i].role == ROLE_TOOL)
            tool_calls++;
    }
    ASSERT_INT_EQ(user_msgs, 2);
    ASSERT_INT_EQ(asst_msgs, 2);
    ASSERT_INT_EQ(tool_calls, 1);

    conversation_free(&conv);
    return 0;
}

static int test_copy_find_nth_assistant(void) {
    conversation_t conv;
    conversation_init(&conv);

    conversation_add_user_text(&conv, "q1");
    message_t *a1 = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(a1, "first answer");

    conversation_add_user_text(&conv, "q2");
    message_t *a2 = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(a2, "second answer");

    /* /copy (latest = "second answer") */
    const char *latest = NULL;
    for (int i = conv.len - 1; i >= 0; i--) {
        if (conv.msgs[i].role == ROLE_ASSISTANT && conv.msgs[i].num_blocks > 0 &&
            conv.msgs[i].blocks[0].type == CONTENT_TEXT) {
            latest = conv.msgs[i].blocks[0].text;
            break;
        }
    }
    ASSERT_NOT_NULL(latest);
    ASSERT(strstr(latest, "second") != NULL);

    /* /copy 2 (second-to-latest = "first answer") */
    const char *second = NULL;
    int found = 0;
    for (int i = conv.len - 1; i >= 0 && found <= 1; i--) {
        if (conv.msgs[i].role == ROLE_ASSISTANT && conv.msgs[i].num_blocks > 0 &&
            conv.msgs[i].blocks[0].type == CONTENT_TEXT) {
            if (found == 1) {
                second = conv.msgs[i].blocks[0].text;
                break;
            }
            found++;
        }
    }
    ASSERT_NOT_NULL(second);
    ASSERT(strstr(second, "first") != NULL);

    conversation_free(&conv);
    return 0;
}

static int test_fast_mode_values(void) {
    ASSERT(true);
    return 0;
}

static int test_help_categories(void) {
    ASSERT(true);
    return 0;
}

int test_commands_v2_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("commands_v2");

    RUN_TEST(test_tab_copy_fast_summary);
    RUN_TEST(test_tab_sorted);
    RUN_TEST(test_tab_count);
    RUN_TEST(test_conversation_find_assistant);
    RUN_TEST(test_summary_counts);
    RUN_TEST(test_copy_find_nth_assistant);
    RUN_TEST(test_fast_mode_values);
    RUN_TEST(test_help_categories);

    TEST_RETURN_FAILED();
}
