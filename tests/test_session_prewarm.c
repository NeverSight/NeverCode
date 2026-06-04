#include "test.h"
#include "nevercode.h"
static int t1(void) {
    session_prewarm_init();
    ASSERT_STR_EQ(session_prewarm_status_name(), "idle");
    ASSERT_FALSE(session_prewarm_ready());
    session_prewarm_free();
    return 0;
}
static int t2(void) {
    session_prewarm_init();
    ASSERT_NULL(session_prewarm_consume());
    session_prewarm_free();
    return 0;
}
static int t3(void) {
    session_prewarm_init();
    ASSERT(session_prewarm_start(NULL, NULL, PROVIDER_COMPAT, 0) < 0);
    session_prewarm_free();
    return 0;
}
static int t4(void) {
    session_prewarm_init();
    ASSERT_INT_EQ((int)session_prewarm_latency_ms(), 0);
    session_prewarm_free();
    return 0;
}
static int t5(void) {
    session_prewarm_init();
    char *f = session_prewarm_format();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "idle");
    free(f);
    session_prewarm_free();
    return 0;
}
static int t6(void) {
    session_prewarm_init();
    session_prewarm_cancel();
    ASSERT_STR_EQ(session_prewarm_status_name(), "idle");
    session_prewarm_free();
    return 0;
}
int test_session_prewarm_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_prewarm");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    TEST_RETURN_FAILED();
}
