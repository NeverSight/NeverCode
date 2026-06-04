#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <unistd.h>
static int t1(void) {
    post_compact_init();
    ASSERT_INT_EQ(post_compact_total(), 0);
    ASSERT_INT_EQ(post_compact_last_turn(), 0);
    return 0;
}
static int t2(void) {
    post_compact_init();
    realtime_ctx_init();
    turn_meta_init();
    approval_cache_init();
    prompt_suggest_init(false);
    int a = post_compact_run(false);
    ASSERT_GT(a, 0);
    ASSERT_INT_EQ(post_compact_total(), 1);
    return 0;
}
static int t3(void) {
    post_compact_init();
    realtime_ctx_init();
    turn_meta_init();
    int a = post_compact_run(true);
    ASSERT_GT(a, 0);
    return 0;
}
static int t4(void) {
    post_compact_init();
    realtime_ctx_init();
    turn_meta_init();
    approval_cache_init();
    prompt_suggest_init(false);
    post_compact_run(false);
    post_compact_run(false);
    ASSERT_INT_EQ(post_compact_total(), 2);
    return 0;
}
static int t5(void) {
    post_compact_init();
    post_compact_set_turn(42);
    ASSERT_INT_EQ(post_compact_last_turn(), 42);
    return 0;
}
static int t6(void) {
    post_compact_init();
    char *s = post_compact_describe();
    ASSERT_STR_CONTAINS(s, "Post-compact");
    free(s);
    return 0;
}
static int t7(void) {
    post_compact_init();
    realtime_ctx_init();
    turn_meta_init();
    approval_cache_init();
    approval_cache_add("ls");
    post_compact_run(true);
    ASSERT_TRUE(approval_cache_check("ls"));
    return 0;
}
static int t8(void) {
    post_compact_init();
    realtime_ctx_init();
    turn_meta_init();
    approval_cache_init();
    prompt_suggest_init(false);
    approval_cache_add("ls");
    post_compact_run(false);
    ASSERT_FALSE(approval_cache_check("ls"));
    return 0;
}
static int t9(void) {
    post_compact_init();
    realtime_ctx_init();
    turn_meta_init();
    approval_cache_init();
    prompt_suggest_init(false);
    FILE *f = fopen("/tmp/nc_test_pc9.txt", "w");
    if (f) {
        fputs("hello", f);
        fclose(f);
    }
    frc_put("/tmp/nc_test_pc9.txt", "hello", 5);
    ASSERT_INT_EQ(frc_size(), 1);
    post_compact_run(false);
    ASSERT_INT_EQ(frc_size(), 0);
    unlink("/tmp/nc_test_pc9.txt");
    return 0;
}
static int t10(void) {
    post_compact_init();
    realtime_ctx_init();
    turn_meta_init();
    compact_warn_suppress();
    ASSERT_TRUE(compact_warn_is_suppressed());
    post_compact_run(true);
    ASSERT_FALSE(compact_warn_is_suppressed());
    return 0;
}
static int t11(void) {
    post_compact_init();
    realtime_ctx_init();
    turn_meta_init();
    FILE *f = fopen("/tmp/nc_test_pc11.txt", "w");
    if (f) {
        fputs("world", f);
        fclose(f);
    }
    frc_put("/tmp/nc_test_pc11.txt", "world", 5);
    ASSERT_INT_EQ(frc_size(), 1);
    post_compact_run(true);
    ASSERT_INT_EQ(frc_size(), 1);
    frc_clear();
    unlink("/tmp/nc_test_pc11.txt");
    return 0;
}
int test_post_compact_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("post_compact");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(t10);
    RUN_TEST(t11);
    TEST_RETURN_FAILED();
}
