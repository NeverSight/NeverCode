#include "test.h"
#include "nevercode.h"

static int test_ac_init(void) {
    auto_continue_init(3);
    ASSERT_TRUE(auto_continue_enabled());
    ASSERT_INT_EQ(auto_continue_retry_count(), 0);
    ASSERT_FALSE(auto_continue_is_continuing());
    auto_continue_free();
    return 0;
}

static int test_ac_no_retry_on_stop(void) {
    auto_continue_init(3);
    bool r = auto_continue_should_retry("stop", "hello", false);
    ASSERT_FALSE(r);
    ASSERT_INT_EQ(auto_continue_retry_count(), 0);
    auto_continue_free();
    return 0;
}

static int test_ac_retry_on_length(void) {
    auto_continue_init(3);
    bool r = auto_continue_should_retry("length", "partial output", false);
    ASSERT_TRUE(r);
    ASSERT_INT_EQ(auto_continue_retry_count(), 1);
    ASSERT_TRUE(auto_continue_is_continuing());
    auto_continue_free();
    return 0;
}

static int test_ac_max_retries(void) {
    auto_continue_init(2);
    ASSERT_TRUE(auto_continue_should_retry("length", "a", false));
    ASSERT_TRUE(auto_continue_should_retry("length", "b", false));
    ASSERT_FALSE(auto_continue_should_retry("length", "c", false));
    ASSERT_INT_EQ(auto_continue_retry_count(), 2);
    auto_continue_free();
    return 0;
}

static int test_ac_stitch(void) {
    auto_continue_init(5);
    auto_continue_should_retry("length", "hello ", false);
    auto_continue_stitch("world");
    char *result = auto_continue_take_result();
    ASSERT_NOT_NULL(result);
    ASSERT_STR_CONTAINS(result, "hello ");
    ASSERT_STR_CONTAINS(result, "world");
    free(result);
    auto_continue_free();
    return 0;
}

static int test_ac_disabled(void) {
    auto_continue_init(3);
    auto_continue_set_enabled(false);
    ASSERT_FALSE(auto_continue_should_retry("length", "x", false));
    auto_continue_free();
    return 0;
}

static int test_ac_reset(void) {
    auto_continue_init(3);
    auto_continue_should_retry("length", "partial", false);
    ASSERT_TRUE(auto_continue_is_continuing());
    auto_continue_reset();
    ASSERT_FALSE(auto_continue_is_continuing());
    ASSERT_INT_EQ(auto_continue_retry_count(), 0);
    auto_continue_free();
    return 0;
}

static int test_ac_prompt(void) {
    auto_continue_init(3);
    const char *p1 = auto_continue_prompt(false);
    ASSERT_NOT_NULL(p1);
    ASSERT_STR_CONTAINS(p1, "truncated");
    const char *p2 = auto_continue_prompt(true);
    ASSERT_NOT_NULL(p2);
    ASSERT_STR_CONTAINS(p2, "tool");
    auto_continue_free();
    return 0;
}

static int test_ac_format(void) {
    auto_continue_init(3);
    auto_continue_should_retry("length", "test", false);
    char *f = auto_continue_format();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Auto-Continue");
    ASSERT_STR_CONTAINS(f, "continuing");
    free(f);
    auto_continue_free();
    return 0;
}

static int test_ac_total_continuations(void) {
    auto_continue_init(10);
    auto_continue_should_retry("length", "a", false);
    auto_continue_reset();
    auto_continue_should_retry("length", "b", false);
    ASSERT_INT_EQ(auto_continue_total_continuations(), 2);
    auto_continue_free();
    return 0;
}

static int test_ac_escalation_pending_on_truncation(void) {
    auto_continue_init(5);
    ASSERT_FALSE(auto_continue_needs_escalation());
    auto_continue_should_retry("length", "partial", false);
    ASSERT_TRUE(auto_continue_needs_escalation());
    ASSERT_INT_EQ(auto_continue_escalated_tokens(), NC_ESCALATED_MAX_TOKENS);
    auto_continue_mark_escalated();
    ASSERT_FALSE(auto_continue_needs_escalation());
    auto_continue_free();
    return 0;
}

static int test_ac_escalation_once_per_turn(void) {
    auto_continue_init(5);
    auto_continue_should_retry("length", "a", false);
    auto_continue_mark_escalated();
    auto_continue_should_retry("length", "b", false);
    ASSERT_FALSE(auto_continue_needs_escalation());
    auto_continue_free();
    return 0;
}

static int test_ac_escalation_not_on_stop(void) {
    auto_continue_init(5);
    auto_continue_should_retry("stop", "done", false);
    ASSERT_FALSE(auto_continue_needs_escalation());
    auto_continue_free();
    return 0;
}

static int test_ac_format_includes_escalation(void) {
    auto_continue_init(3);
    auto_continue_should_retry("length", "x", false);
    auto_continue_mark_escalated();
    char *f = auto_continue_format();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "escalations=1");
    free(f);
    auto_continue_free();
    return 0;
}

int test_auto_continue_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("auto_continue");
    RUN_TEST(test_ac_init);
    RUN_TEST(test_ac_no_retry_on_stop);
    RUN_TEST(test_ac_retry_on_length);
    RUN_TEST(test_ac_max_retries);
    RUN_TEST(test_ac_stitch);
    RUN_TEST(test_ac_disabled);
    RUN_TEST(test_ac_reset);
    RUN_TEST(test_ac_prompt);
    RUN_TEST(test_ac_format);
    RUN_TEST(test_ac_total_continuations);
    RUN_TEST(test_ac_escalation_pending_on_truncation);
    RUN_TEST(test_ac_escalation_once_per_turn);
    RUN_TEST(test_ac_escalation_not_on_stop);
    RUN_TEST(test_ac_format_includes_escalation);
    TEST_RETURN_FAILED();
}
