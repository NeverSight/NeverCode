#include "test.h"
#include "nevercode.h"
static int t1(void) {
    query_mode_init("test", 10000);
    ASSERT_TRUE(query_mode_active());
    ASSERT_INT_EQ(query_mode_budget(), 10000);
    ASSERT_FALSE(query_mode_should_stop());
    query_mode_free();
    return 0;
}
static int t2(void) {
    query_mode_init("t", 1000);
    ASSERT_FALSE(query_mode_consume(500));
    ASSERT_INT_EQ(query_mode_remaining(), 500);
    ASSERT_TRUE(query_mode_consume(600));
    ASSERT_TRUE(query_mode_over_budget());
    query_mode_free();
    return 0;
}
static int t3(void) {
    query_mode_init("t", 50000);
    query_mode_set_max_tools(3);
    query_mode_record_tool();
    query_mode_record_tool();
    ASSERT_FALSE(query_mode_should_stop());
    query_mode_record_tool();
    ASSERT_TRUE(query_mode_should_stop());
    query_mode_free();
    return 0;
}
static int t4(void) {
    query_mode_init("t", 50000);
    query_mode_stop();
    ASSERT_TRUE(query_mode_should_stop());
    query_mode_free();
    return 0;
}
static int t5(void) {
    query_mode_init("t", 5000);
    query_mode_consume(2500);
    char *s = query_mode_format();
    ASSERT_STR_CONTAINS(s, "2500/5000");
    free(s);
    query_mode_free();
    return 0;
}
static int hf = 0;
static void th(void *c) {
    (void)c;
    hf = 1;
}
static int t6(void) {
    query_mode_init("t", 50000);
    hf = 0;
    query_mode_add_stop_hook(th, NULL);
    query_mode_stop();
    ASSERT_INT_EQ(hf, 1);
    query_mode_free();
    return 0;
}
int test_query_mode_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("query_mode");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    TEST_RETURN_FAILED();
}
