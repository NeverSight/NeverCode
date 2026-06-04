#include "test.h"
#include "nevercode.h"

/* Forward declarations from ctx_history.c */
void ctx_history_init(int budget, int recent_keep);
void ctx_history_clear(void);
int ctx_history_add(int token_est, int anchor_flags);
int ctx_history_trim(void);
int ctx_history_active_count(void);
int ctx_history_total_tokens(void);
int ctx_history_count(void);
int ctx_history_is_dropped(int index);
int ctx_history_set_summary(int index, const char *summary);
const char *ctx_history_get_summary(int index);
int ctx_history_pin(int index);
int ctx_history_unpin(int index);
char *ctx_history_format_stats(void);

static int test_init_add(void) {
    ctx_history_init(1000, 2);
    ASSERT_INT_EQ(ctx_history_count(), 0);
    int idx = ctx_history_add(100, 0);
    ASSERT_INT_EQ(idx, 0);
    ASSERT_INT_EQ(ctx_history_count(), 1);
    ASSERT_INT_EQ(ctx_history_total_tokens(), 100);
    ctx_history_clear();
    return 0;
}

static int test_trim_drops_old(void) {
    ctx_history_init(300, 1);
    ctx_history_add(100, 0);
    ctx_history_add(100, 0);
    ctx_history_add(100, 0);
    ctx_history_add(100, 0);
    ASSERT_INT_EQ(ctx_history_total_tokens(), 400);

    int d = ctx_history_trim();
    ASSERT_GT(d, 0);
    ASSERT_TRUE(ctx_history_total_tokens() <= 300);
    ctx_history_clear();
    return 0;
}

static int test_trim_preserves_anchored(void) {
    ctx_history_init(100, 1);
    ctx_history_add(80, 0x01); /* system anchor */
    ctx_history_add(80, 0);
    ctx_history_add(80, 0);

    ctx_history_trim();
    ASSERT_FALSE(ctx_history_is_dropped(0));
    ctx_history_clear();
    return 0;
}

static int test_trim_preserves_recent(void) {
    ctx_history_init(100, 2);
    ctx_history_add(60, 0);
    ctx_history_add(60, 0);
    ctx_history_add(60, 0);

    ctx_history_trim();
    /* last 2 should be kept */
    ASSERT_FALSE(ctx_history_is_dropped(1));
    ASSERT_FALSE(ctx_history_is_dropped(2));
    ctx_history_clear();
    return 0;
}

static int test_pin_unpin(void) {
    /* recent_keep=1 so only the last item is protected */
    ctx_history_init(100, 1);
    ctx_history_add(60, 0);
    ctx_history_add(60, 0);
    ctx_history_add(60, 0);

    ctx_history_trim();
    ASSERT_TRUE(ctx_history_is_dropped(0));

    ctx_history_clear();
    ctx_history_init(100, 1);
    ctx_history_add(60, 0);
    ctx_history_add(60, 0);
    ctx_history_add(60, 0);
    ctx_history_pin(0);
    ctx_history_trim();
    ASSERT_FALSE(ctx_history_is_dropped(0));

    ctx_history_unpin(0);
    ctx_history_clear();
    return 0;
}

static int test_summary(void) {
    ctx_history_init(1000, 2);
    ctx_history_add(100, 0);
    ASSERT_INT_EQ(ctx_history_set_summary(0, "test summary"), 0);
    ASSERT_STR_EQ(ctx_history_get_summary(0), "test summary");
    ASSERT_NULL(ctx_history_get_summary(5));
    ctx_history_clear();
    return 0;
}

static int test_format_stats(void) {
    ctx_history_init(500, 2);
    ctx_history_add(100, 0);
    ctx_history_add(200, 0);
    char *s = ctx_history_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "History:");
    free(s);
    ctx_history_clear();
    return 0;
}

int test_ctx_history_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("ctx_history");
    RUN_TEST(test_init_add);
    RUN_TEST(test_trim_drops_old);
    RUN_TEST(test_trim_preserves_anchored);
    RUN_TEST(test_trim_preserves_recent);
    RUN_TEST(test_pin_unpin);
    RUN_TEST(test_summary);
    RUN_TEST(test_format_stats);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("ctx_history");
    RUN_TEST(test_init_add);
    RUN_TEST(test_trim_drops_old);
    RUN_TEST(test_trim_preserves_anchored);
    RUN_TEST(test_trim_preserves_recent);
    RUN_TEST(test_pin_unpin);
    RUN_TEST(test_summary);
    RUN_TEST(test_format_stats);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
