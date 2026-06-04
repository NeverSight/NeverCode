/*
 * test_ctx_token_stats.c — Tests for context token distribution analysis.
 */
#include "test.h"
#include "nevercode.h"

static int t_init_zeros(void) {
    ctx_token_stats_t s;
    ctx_token_stats_init(&s);
    ASSERT_INT_EQ(s.total_tokens, 0);
    ASSERT_INT_EQ(s.user_tokens, 0);
    ASSERT_INT_EQ(s.assistant_tokens, 0);
    ASSERT_INT_EQ(s.tool_type_count, 0);
    ASSERT_INT_EQ(s.dup_read_count, 0);
    return 0;
}

static int t_null_safe(void) {
    ctx_token_stats_t s;
    ctx_token_stats_analyze(&s, NULL, 0);
    ASSERT_INT_EQ(s.total_tokens, 0);

    ctx_token_stats_analyze(NULL, NULL, 0);

    char *fmt = ctx_token_stats_format(NULL);
    ASSERT_NULL(fmt);
    return 0;
}

static int t_format_empty(void) {
    ctx_token_stats_t s;
    ctx_token_stats_init(&s);
    char *fmt = ctx_token_stats_format(&s);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "empty conversation");
    free(fmt);
    return 0;
}

static int t_basic_analysis(void) {
    message_t msgs[3];
    memset(msgs, 0, sizeof(msgs));

    content_block_t blk0 = {.type = CONTENT_TEXT, .text = nc_xstrdup("Hello world")};
    msgs[0].role = ROLE_USER;
    msgs[0].blocks = &blk0;
    msgs[0].num_blocks = 1;

    content_block_t blk1 = {.type = CONTENT_TEXT, .text = nc_xstrdup("Hi there, how can I help?")};
    msgs[1].role = ROLE_ASSISTANT;
    msgs[1].blocks = &blk1;
    msgs[1].num_blocks = 1;

    content_block_t blk2 = {.type = CONTENT_TEXT, .text = nc_xstrdup("Thanks!")};
    msgs[2].role = ROLE_USER;
    msgs[2].blocks = &blk2;
    msgs[2].num_blocks = 1;

    ctx_token_stats_t s;
    ctx_token_stats_analyze(&s, msgs, 3);

    ASSERT(s.total_tokens > 0);
    ASSERT(s.user_tokens > 0);
    ASSERT_INT_EQ(s.user_count, 2);
    ASSERT(s.assistant_tokens > 0);
    ASSERT_INT_EQ(s.assistant_count, 1);
    ASSERT_INT_EQ(s.tool_result_count, 0);

    char *fmt = ctx_token_stats_format(&s);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "User:");
    ASSERT_STR_CONTAINS(fmt, "Assistant:");
    free(fmt);

    free(blk0.text);
    free(blk1.text);
    free(blk2.text);
    return 0;
}

static int t_macros(void) {
    ASSERT(CTX_STATS_TOOL_MAX > 0);
    ASSERT(CTX_STATS_DUP_READ_MAX > 0);
    ASSERT(FRC_KEEP_RECENT > 0);
    return 0;
}

int test_ctx_token_stats_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("ctx_token_stats");
    RUN_TEST(t_init_zeros);
    RUN_TEST(t_null_safe);
    RUN_TEST(t_format_empty);
    RUN_TEST(t_basic_analysis);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
