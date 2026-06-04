#include "test.h"
#include "nevercode.h"
#include <unistd.h>

static int test_init(void) {
    session_stats_init();
    ASSERT_INT_EQ(session_stats_total_sessions(), 0);
    ASSERT_INT_EQ((int)session_stats_total_tokens(), 0);
    ASSERT(session_stats_total_cost() < 0.001);
    session_stats_free();
    return 0;
}

static int test_record_session(void) {
    session_stats_init();
    session_stats_record_session("gpt-5.4", 1000, 2000, 0.05, 120, 10, 5, 100, 50);
    ASSERT_INT_EQ(session_stats_total_sessions(), 1);
    ASSERT_INT_EQ((int)session_stats_total_tokens(), 3000);
    ASSERT(session_stats_total_cost() > 0.04 && session_stats_total_cost() < 0.06);
    ASSERT_INT_EQ((int)session_stats_total_duration(), 120);
    session_stats_free();
    return 0;
}

static int test_record_multiple(void) {
    session_stats_init();
    session_stats_record_session("gpt-5.4", 1000, 500, 0.02, 60, 5, 3, 20, 10);
    session_stats_record_session("gpt-5.4", 2000, 1000, 0.04, 90, 8, 4, 30, 15);
    session_stats_record_session("claude-3", 500, 300, 0.01, 30, 2, 1, 10, 5);
    ASSERT_INT_EQ(session_stats_total_sessions(), 3);
    ASSERT_INT_EQ((int)session_stats_total_tokens(), 5300);
    ASSERT_INT_EQ(session_stats_model_count(), 2);
    session_stats_free();
    return 0;
}

static int test_save_load_roundtrip(void) {
    session_stats_init();
    session_stats_record_session("test-model", 500, 250, 0.01, 30, 3, 2, 10, 5);

    int rc = session_stats_save();
    ASSERT_INT_EQ(rc, 0);

    session_stats_free();
    session_stats_init();

    rc = session_stats_load();
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(session_stats_total_sessions(), 1);
    ASSERT_INT_EQ((int)session_stats_total_tokens(), 750);
    ASSERT_INT_EQ(session_stats_model_count(), 1);

    session_stats_free();
    return 0;
}

static int test_format(void) {
    session_stats_init();
    session_stats_record_session("gpt-5.4", 10000, 5000, 0.15, 300, 20, 10, 200, 100);
    char *s = session_stats_format();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "1 sessions") != NULL);
    ASSERT(strstr(s, "Tokens") != NULL);
    ASSERT(strstr(s, "Cost") != NULL);
    ASSERT(strstr(s, "gpt-5.4") != NULL);
    free(s);
    session_stats_free();
    return 0;
}

static int test_format_brief(void) {
    session_stats_init();
    session_stats_record_session("gpt-5.4", 10000, 5000, 0.50, 3600, 20, 10, 200, 100);
    char *s = session_stats_format_brief();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "1 sessions") != NULL);
    ASSERT(strstr(s, "15000 tokens") != NULL);
    free(s);
    session_stats_free();
    return 0;
}

static int test_format_empty(void) {
    session_stats_init();
    char *s = session_stats_format();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "0 sessions") != NULL);
    free(s);
    session_stats_free();
    return 0;
}

static int test_accumulate(void) {
    session_stats_init();
    for (int i = 0; i < 5; i++) {
        session_stats_record_session("model-x", 100, 50, 0.002, 10, 1, 1, 5, 3);
    }
    ASSERT_INT_EQ(session_stats_total_sessions(), 5);
    ASSERT_INT_EQ((int)session_stats_total_tokens(), 750);
    ASSERT_INT_EQ((int)session_stats_total_duration(), 50);
    session_stats_free();
    return 0;
}

static int test_null_model(void) {
    session_stats_init();
    session_stats_record_session(NULL, 100, 50, 0.001, 10, 1, 0, 0, 0);
    ASSERT_INT_EQ(session_stats_total_sessions(), 1);
    ASSERT_INT_EQ(session_stats_model_count(), 0);
    session_stats_free();
    return 0;
}

int test_session_stats_all(void) {
    TEST_SUITE("session_stats");
    TEST_COUNTERS;

    RUN_TEST(test_init);
    RUN_TEST(test_record_session);
    RUN_TEST(test_record_multiple);
    RUN_TEST(test_save_load_roundtrip);
    RUN_TEST(test_format);
    RUN_TEST(test_format_brief);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_accumulate);
    RUN_TEST(test_null_model);

    TEST_RETURN_FAILED();
}
