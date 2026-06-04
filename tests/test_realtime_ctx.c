#include "test.h"
#include "nevercode.h"
static int t1(void) {
    realtime_ctx_init();
    ASSERT_INT_EQ(realtime_ctx_probe_count(), 0);
    realtime_ctx_free();
    return 0;
}
static int t2(void) {
    realtime_ctx_init();
    realtime_ctx_refresh();
    ASSERT_TRUE(realtime_ctx_probe_count() > 0);
    ASSERT_NOT_NULL(realtime_ctx_get("cwd"));
    realtime_ctx_free();
    return 0;
}
static int t3(void) {
    realtime_ctx_init();
    ASSERT_NULL(realtime_ctx_get("nope"));
    realtime_ctx_free();
    return 0;
}
static int t4(void) {
    realtime_ctx_init();
    char *s = realtime_ctx_build_section();
    ASSERT_STR_CONTAINS(s, "<environment_context>");
    ASSERT_STR_CONTAINS(s, "Working Directory:");
    free(s);
    realtime_ctx_free();
    return 0;
}
static int t5(void) {
    realtime_ctx_init();
    ASSERT_TRUE(realtime_ctx_stale(0));
    realtime_ctx_refresh();
    ASSERT_FALSE(realtime_ctx_stale(60));
    realtime_ctx_free();
    return 0;
}
static int t6(void) {
    realtime_ctx_init();
    realtime_ctx_refresh();
    int c1 = realtime_ctx_probe_count();
    realtime_ctx_refresh();
    ASSERT_INT_EQ(c1, realtime_ctx_probe_count());
    realtime_ctx_free();
    return 0;
}
int test_realtime_ctx_all(void) {
    TEST_SUITE("realtime_ctx");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    TEST_RETURN_FAILED();
}
