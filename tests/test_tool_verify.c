#include "test.h"
#include "harness.h"
#include "defaults.h"
static int test_tv_init(void) {
    tool_verify_init();
    ASSERT_INT_EQ(tool_verify_total_count(), 0);
    tool_verify_free();
    return 0;
}
static int test_tv_pass(void) {
    tool_verify_init();
    tv_verdict_t v = tool_verify_check("bash", "hello", 5, 0, false);
    ASSERT_INT_EQ(v, TV_PASS);
    tool_verify_free();
    return 0;
}
static int test_tv_fail(void) {
    tool_verify_init();
    tv_verdict_t v = tool_verify_check("bash", "err", 3, 1, true);
    ASSERT_INT_EQ(v, TV_EXPLICIT_FAIL);
    ASSERT_TRUE(tool_verify_needs_intervention(v));
    tool_verify_free();
    return 0;
}
static int test_tv_empty_read(void) {
    tool_verify_init();
    tv_verdict_t v = tool_verify_check("file_read", NULL, 0, 0, false);
    ASSERT_INT_EQ(v, TV_EMPTY_OUTPUT);
    tool_verify_free();
    return 0;
}
static int test_tv_write_pass(void) {
    tool_verify_init();
    tv_verdict_t v = tool_verify_check("file_write", NULL, 0, 0, false);
    ASSERT_INT_EQ(v, TV_PASS);
    tool_verify_free();
    return 0;
}
static int test_tv_suspicious(void) {
    tool_verify_init();
    tv_verdict_t v = tool_verify_check("grep", "ab", 2, 0, false);
    ASSERT_INT_EQ(v, TV_SUSPICIOUS);
    tool_verify_free();
    return 0;
}
static int test_tv_pattern(void) {
    tool_verify_init();
    tool_verify_check("glob", NULL, 0, 0, false);
    tool_verify_check("glob", NULL, 0, 0, false);
    tv_verdict_t v = tool_verify_check("glob", NULL, 0, 0, false);
    ASSERT_INT_EQ(v, TV_PATTERN_ALERT);
    tool_verify_free();
    return 0;
}
static int test_tv_reset(void) {
    tool_verify_init();
    tool_verify_check("glob", NULL, 0, 0, false);
    tool_verify_check("glob", NULL, 0, 0, false);
    tool_verify_check("bash", "out", 3, 0, false);
    tv_verdict_t v = tool_verify_check("glob", NULL, 0, 0, false);
    ASSERT_INT_EQ(v, TV_EMPTY_OUTPUT);
    tool_verify_free();
    return 0;
}
static int test_tv_silent_error(void) {
    tool_verify_init();
    tv_verdict_t v = tool_verify_check("bash", "ls: permission denied", 21, 0, false);
    ASSERT_INT_EQ(v, TV_SUSPICIOUS);
    tool_verify_free();
    return 0;
}
static int test_tv_silent_segfault(void) {
    tool_verify_init();
    tv_verdict_t v = tool_verify_check("bash", "Segmentation fault (core dumped)", 32, 0, false);
    ASSERT_INT_EQ(v, TV_SUSPICIOUS);
    tool_verify_free();
    return 0;
}
static int test_tv_silent_oom(void) {
    tool_verify_init();
    tv_verdict_t v = tool_verify_check("bash", "Cannot allocate: out of memory", 31, 0, false);
    ASSERT_INT_EQ(v, TV_SUSPICIOUS);
    tool_verify_free();
    return 0;
}
static int test_tv_silent_write_ignored(void) {
    tool_verify_init();
    tv_verdict_t v =
        tool_verify_check("file_write", "permission denied on /etc/passwd", 32, 0, false);
    ASSERT_INT_EQ(v, TV_PASS);
    tool_verify_free();
    return 0;
}
static int test_tv_names(void) {
    ASSERT_STR_EQ(tv_verdict_name(TV_PASS), "pass");
    ASSERT_STR_EQ(tv_verdict_name(TV_PATTERN_ALERT), "pattern_alert");
    return 0;
}
static int test_tv_format(void) {
    tool_verify_init();
    tool_verify_check("bash", "ok", 2, 0, false);
    char *s = tool_verify_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Verified: 1");
    free(s);
    tool_verify_free();
    return 0;
}

static int test_tv_snapshot_restore(void) {
    tool_verify_init();
    tool_verify_check("glob", NULL, 0, 0, false);
    tool_verify_check("glob", NULL, 0, 0, false);

    tool_verify_snapshot_t snap;
    tool_verify_snapshot(&snap);
    ASSERT_INT_EQ(snap.count, 2);
    ASSERT_INT_EQ(snap.consecutive_empty, 2);

    tool_verify_free();
    tool_verify_restore(&snap);
    ASSERT_INT_EQ(tool_verify_total_count(), 2);
    ASSERT_INT_EQ(tool_verify_flagged_count(), 2);

    char *s = tool_verify_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Consecutive empty: 2");
    free(s);

    tv_verdict_t v = tool_verify_check("glob", NULL, 0, 0, false);
    ASSERT_INT_EQ(v, TV_PATTERN_ALERT);
    tool_verify_free();
    return 0;
}

int test_tool_verify_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_verify");
    RUN_TEST(test_tv_init);
    RUN_TEST(test_tv_pass);
    RUN_TEST(test_tv_fail);
    RUN_TEST(test_tv_empty_read);
    RUN_TEST(test_tv_write_pass);
    RUN_TEST(test_tv_suspicious);
    RUN_TEST(test_tv_pattern);
    RUN_TEST(test_tv_reset);
    RUN_TEST(test_tv_silent_error);
    RUN_TEST(test_tv_silent_segfault);
    RUN_TEST(test_tv_silent_oom);
    RUN_TEST(test_tv_silent_write_ignored);
    RUN_TEST(test_tv_names);
    RUN_TEST(test_tv_format);
    RUN_TEST(test_tv_snapshot_restore);
    TEST_RETURN_FAILED();
}
