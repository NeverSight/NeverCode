#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init_and_append(void) {
    static shared_pad_t sp;
    shared_pad_init(&sp);
    ASSERT_INT_EQ(shared_pad_line_count(&sp), 0);
    ASSERT_INT_EQ(shared_pad_append(&sp, "agent-1", "finding A"), 0);
    ASSERT_INT_EQ(shared_pad_line_count(&sp), 1);
    shared_pad_free(&sp);
    return 0;
}

static int t_merge_dedup(void) {
    static shared_pad_t sp;
    shared_pad_init(&sp);
    shared_pad_merge(&sp, "a1", "line1\nline2\nline1");
    ASSERT_INT_EQ(shared_pad_line_count(&sp), 2);
    shared_pad_free(&sp);
    return 0;
}

static int t_read_content(void) {
    static shared_pad_t sp;
    shared_pad_init(&sp);
    shared_pad_append(&sp, "bot", "hello world");
    char *out = shared_pad_read(&sp);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "hello world");
    ASSERT_STR_CONTAINS(out, "[bot]");
    free(out);
    shared_pad_free(&sp);
    return 0;
}

static int t_snapshot_restore(void) {
    static shared_pad_t sp;
    shared_pad_init(&sp);
    shared_pad_append(&sp, "x", "data");

    shared_pad_snapshot_t snap;
    shared_pad_snapshot(&sp, &snap);
    ASSERT_INT_EQ(snap.count, 1);

    static shared_pad_t sp2;
    shared_pad_init(&sp2);
    shared_pad_restore(&sp2, &snap);
    ASSERT_INT_EQ(shared_pad_line_count(&sp2), 1);

    shared_pad_free(&sp);
    shared_pad_free(&sp2);
    return 0;
}

static int t_empty_text_rejected(void) {
    static shared_pad_t sp;
    shared_pad_init(&sp);
    ASSERT_INT_EQ(shared_pad_append(&sp, "a", ""), -1);
    ASSERT_INT_EQ(shared_pad_append(&sp, "a", NULL), -1);
    ASSERT_INT_EQ(shared_pad_line_count(&sp), 0);
    shared_pad_free(&sp);
    return 0;
}

static int t_run_token(void) {
    static shared_pad_t sp;
    shared_pad_init(&sp);
    shared_pad_set_run_token(&sp, "tok-42");
    ASSERT_STR_EQ(sp.current_run_token, "tok-42");
    shared_pad_free(&sp);
    return 0;
}

int test_shared_pad_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("shared_pad");
    RUN_TEST(t_init_and_append);
    RUN_TEST(t_merge_dedup);
    RUN_TEST(t_read_content);
    RUN_TEST(t_snapshot_restore);
    RUN_TEST(t_empty_text_rejected);
    RUN_TEST(t_run_token);
    TEST_RETURN_FAILED();
}
