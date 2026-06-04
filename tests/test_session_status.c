#include "test.h"
#include "nevercode.h"

static int t_init(void) {
    session_status_init();
    ASSERT_TRUE(session_status_is_idle());
    ASSERT_FALSE(session_status_is_busy());
    ASSERT_STR_EQ(session_status_name(), "idle");
    return 0;
}

static int t_busy(void) {
    session_status_init();
    session_status_set_busy();
    ASSERT_TRUE(session_status_is_busy());
    ASSERT_FALSE(session_status_is_idle());
    ASSERT_STR_EQ(session_status_name(), "busy");
    return 0;
}

static int t_retry(void) {
    session_status_init();
    session_status_set_retry(2, "rate limited", 5000);
    ASSERT_TRUE(session_status_is_busy());
    ASSERT_STR_EQ(session_status_name(), "retry");
    ASSERT_INT_EQ(session_status_retry_attempt(), 2);
    return 0;
}

static int t_idle_after_busy(void) {
    session_status_init();
    session_status_set_busy();
    session_status_set_idle();
    ASSERT_TRUE(session_status_is_idle());
    return 0;
}

static int t_compact(void) {
    session_status_init();
    session_status_set_compact();
    ASSERT_TRUE(session_status_is_busy());
    ASSERT_STR_EQ(session_status_name(), "compact");
    return 0;
}

static int t_format(void) {
    session_status_init();
    session_status_set_error("test error");
    char *s = session_status_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "error");
    ASSERT_STR_CONTAINS(s, "test error");
    free(s);
    return 0;
}

int test_session_status_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_status");
    RUN_TEST(t_init);
    RUN_TEST(t_busy);
    RUN_TEST(t_retry);
    RUN_TEST(t_idle_after_busy);
    RUN_TEST(t_compact);
    RUN_TEST(t_format);
    TEST_RETURN_FAILED();
}
