#include "test.h"
#include "nevercode.h"
static int t1(void) {
    auto_dream_init();
    ASSERT_TRUE(auto_dream_enabled());
    ASSERT_FALSE(auto_dream_running());
    ASSERT_INT_EQ(auto_dream_total_runs(), 0);
    auto_dream_free();
    return 0;
}
static int t2(void) {
    auto_dream_init();
    auto_dream_set_enabled(false);
    ASSERT_FALSE(auto_dream_enabled());
    auto_dream_set_enabled(true);
    ASSERT_TRUE(auto_dream_enabled());
    auto_dream_free();
    return 0;
}
static int t3(void) {
    auto_dream_init();
    auto_dream_set_enabled(false);
    ASSERT_INT_EQ(auto_dream_check(), 0);
    auto_dream_free();
    return 0;
}
static int t4(void) {
    auto_dream_init();
    auto_dream_touch();
    ASSERT_INT_EQ(auto_dream_check(), 0);
    auto_dream_free();
    return 0;
}
static int t5(void) {
    auto_dream_init();
    char *s = auto_dream_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Background Consolidation");
    free(s);
    auto_dream_free();
    return 0;
}
static int t6(void) {
    auto_dream_init();
    auto_dream_set_enabled(false);
    char *s = auto_dream_status();
    ASSERT_STR_CONTAINS(s, "disabled");
    free(s);
    auto_dream_free();
    return 0;
}
static int t7(void) {
    auto_dream_init();
    ASSERT_FALSE(auto_dream_running());
    auto_dream_free();
    return 0;
}
static int t8(void) {
    auto_dream_init();
    ASSERT_INT_EQ(auto_dream_sessions_reviewed(), 0);
    ASSERT_INT_EQ(auto_dream_files_touched(), 0);
    auto_dream_free();
    return 0;
}
static int t9(void) {
    auto_dream_init();
    auto_dream_free();
    auto_dream_init();
    ASSERT_TRUE(auto_dream_enabled());
    auto_dream_free();
    return 0;
}
int test_auto_dream_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("auto_dream");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    TEST_RETURN_FAILED();
}
