#include "test.h"
#include "nevercode.h"
#include "conversation.h"
#include "repl.h"
#include <time.h>

static int test_snip_tool_message(void) {
    message_t m;
    message_init(&m, ROLE_TOOL);
    message_add_tool_result(&m, "id1", "This is a long tool result with lots of content", 0);
    int freed = context_snip_message(&m);
    ASSERT(freed > 0);
    ASSERT_STR_EQ(m.blocks[0].text, "[Old tool result content cleared]");
    message_free(&m);
    return 0;
}

static int test_snip_already_snipped(void) {
    message_t m;
    message_init(&m, ROLE_TOOL);
    message_add_tool_result(&m, "id1", "[Old tool result content cleared]", 0);
    int freed = context_snip_message(&m);
    ASSERT_INT_EQ(freed, 0);
    message_free(&m);
    return 0;
}

static int test_snip_non_tool(void) {
    message_t m;
    message_init(&m, ROLE_USER);
    message_add_text(&m, "Hello world");
    int freed = context_snip_message(&m);
    ASSERT_INT_EQ(freed, 0);
    message_free(&m);
    return 0;
}

static int test_snip_range(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "hello");
    message_t *tm = conversation_add(&conv, ROLE_TOOL);
    message_add_tool_result(tm, "id1", "result data here that is long enough", 0);
    message_t *tm2 = conversation_add(&conv, ROLE_TOOL);
    message_add_tool_result(tm2, "id2", "more result data", 0);
    conversation_add_user_text(&conv, "next");

    int freed = context_snip_range(&conv, 0, 3);
    ASSERT(freed > 0);
    ASSERT_STR_EQ(conv.msgs[1].blocks[0].text, "[Old tool result content cleared]");
    ASSERT_STR_EQ(conv.msgs[2].blocks[0].text, "[Old tool result content cleared]");
    conversation_free(&conv);
    return 0;
}

static int test_micro_compact_skip_recent(void) {
    conversation_t conv;
    conversation_init(&conv);
    for (int i = 0; i < 3; i++) {
        conversation_add_user_text(&conv, "q");
        message_t *tm = conversation_add(&conv, ROLE_TOOL);
        message_add_tool_result(tm, "id", "short", 0);
    }
    int freed = context_micro_compact(&conv, nc_time_now(), 300);
    ASSERT_INT_EQ(freed, 0);
    conversation_free(&conv);
    return 0;
}

static int test_micro_compact_clears_old(void) {
    conversation_t conv;
    conversation_init(&conv);
    for (int i = 0; i < 8; i++) {
        conversation_add_user_text(&conv, "question");
        message_t *tm = conversation_add(&conv, ROLE_TOOL);
        char result[256];
        snprintf(result, sizeof(result),
                 "Tool result content for iteration %d with enough text to matter", i);
        message_add_tool_result(tm, "id", result, 0);
        tm->blocks[0].tool_name = nc_xstrdup("Read");
    }
    /* Force cache expired (last_assistant_time = 0 means very old) */
    int freed = context_micro_compact(&conv, 0, 300);
    ASSERT(freed > 0);
    conversation_free(&conv);
    return 0;
}

static int test_estimate_freed_tokens(void) {
    ASSERT_INT_EQ(context_estimate_freed_tokens(400), 100);
    ASSERT_INT_EQ(context_estimate_freed_tokens(0), 0);
    ASSERT_INT_EQ(context_estimate_freed_tokens(1000), 250);
    return 0;
}

static int test_snip_null(void) {
    ASSERT_INT_EQ(context_snip_message(NULL), 0);
    ASSERT_INT_EQ(context_snip_range(NULL, 0, 10), 0);
    return 0;
}

/* ── Level 3: Collapse tests ── */

static int test_collapse_short_conv(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "q");
    int freed = context_collapse(&conv, 3);
    ASSERT_INT_EQ(freed, 0);
    conversation_free(&conv);
    return 0;
}

static int test_collapse_null(void) {
    ASSERT_INT_EQ(context_collapse(NULL, 3), 0);
    return 0;
}

static int test_collapse_folds_old_rounds(void) {
    conversation_t conv;
    conversation_init(&conv);

    for (int i = 0; i < 8; i++) {
        conversation_add_user_text(&conv, "question");
        message_t *am = conversation_add(&conv, ROLE_ASSISTANT);
        char buf[800];
        memset(buf, 'x', sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        message_add_text(am, buf);
        message_add_tool_use(am, "tu", "Read", NULL);
        message_t *tm = conversation_add(&conv, ROLE_TOOL);
        char result[1024];
        memset(result, 'y', sizeof(result) - 1);
        result[sizeof(result) - 1] = '\0';
        message_add_tool_result(tm, "tu", result, 0);
        tm->blocks[0].tool_name = nc_xstrdup("Read");
    }

    int freed = context_collapse(&conv, 3);
    ASSERT(freed > 0);
    conversation_free(&conv);
    return 0;
}

static int test_collapse_needed_thresholds(void) {
    ASSERT(context_collapse_needed(800, 1000) == true);
    ASSERT(context_collapse_needed(700, 1000) == false);
    ASSERT(context_collapse_needed(0, 1000) == false);
    ASSERT(context_collapse_needed(100, 0) == false);
    return 0;
}

static int test_compact_metrics_record(void) {
    compact_metrics_reset();
    ASSERT_INT_EQ(compact_metrics_total_invocations(), 0);
    ASSERT(compact_metrics_total_tokens_saved() == 0);

    compact_metrics_record(1, 400);
    ASSERT_INT_EQ(compact_metrics_total_invocations(), 1);
    ASSERT(compact_metrics_total_tokens_saved() > 0);

    compact_metrics_record(2, 800);
    ASSERT_INT_EQ(compact_metrics_total_invocations(), 2);

    char *fmt = compact_metrics_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "Compactions: 2");
    free(fmt);

    compact_metrics_reset();
    ASSERT_INT_EQ(compact_metrics_total_invocations(), 0);
    return 0;
}

int test_context_compact_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("context_compact");
    RUN_TEST(test_snip_tool_message);
    RUN_TEST(test_snip_already_snipped);
    RUN_TEST(test_snip_non_tool);
    RUN_TEST(test_snip_range);
    RUN_TEST(test_micro_compact_skip_recent);
    RUN_TEST(test_micro_compact_clears_old);
    RUN_TEST(test_estimate_freed_tokens);
    RUN_TEST(test_snip_null);
    RUN_TEST(test_collapse_short_conv);
    RUN_TEST(test_collapse_null);
    RUN_TEST(test_collapse_folds_old_rounds);
    RUN_TEST(test_collapse_needed_thresholds);
    RUN_TEST(test_compact_metrics_record);
    TEST_RETURN_FAILED();
}
