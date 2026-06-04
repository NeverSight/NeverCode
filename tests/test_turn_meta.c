#include "test.h"
#include "nevercode.h"
static int t1(void) {
    turn_meta_init();
    ASSERT_INT_EQ(turn_meta_count(), 0);
    turn_meta_free();
    return 0;
}
static int t2(void) {
    turn_meta_init();
    ASSERT_INT_EQ(turn_meta_begin(1), 0);
    ASSERT_INT_EQ(turn_meta_count(), 1);
    turn_meta_end();
    turn_meta_free();
    return 0;
}
static int t3(void) {
    turn_meta_init();
    turn_meta_begin(1);
    turn_meta_set_tokens(1000, 500);
    char *s = turn_meta_format(0);
    ASSERT_STR_CONTAINS(s, "1000");
    free(s);
    turn_meta_end();
    turn_meta_free();
    return 0;
}
static int t4(void) {
    turn_meta_init();
    turn_meta_begin(1);
    turn_meta_record_tool("Bash", 100);
    turn_meta_record_tool("Read", 50);
    turn_meta_record_tool("Bash", 200);
    ASSERT_INT_EQ(turn_meta_total_tools(), 3);
    char *s = turn_meta_format(0);
    ASSERT_STR_CONTAINS(s, "Bash");
    free(s);
    turn_meta_end();
    turn_meta_free();
    return 0;
}
static int t5(void) {
    turn_meta_init();
    turn_meta_begin(1);
    turn_meta_record_file_read();
    turn_meta_record_file_write(10, 3);
    char *s = turn_meta_format(0);
    ASSERT_STR_CONTAINS(s, "1 read");
    free(s);
    turn_meta_end();
    turn_meta_free();
    return 0;
}
static int t6(void) {
    turn_meta_init();
    turn_meta_begin(1);
    turn_meta_record_error();
    turn_meta_record_error();
    turn_meta_record_retry();
    ASSERT_INT_EQ(turn_meta_total_errors(), 2);
    char *s = turn_meta_format(0);
    ASSERT_STR_CONTAINS(s, "Errors: 2");
    free(s);
    turn_meta_end();
    turn_meta_free();
    return 0;
}
static int t7(void) {
    turn_meta_init();
    turn_meta_begin(1);
    turn_meta_mark_compacted();
    char *s = turn_meta_format(0);
    ASSERT_STR_CONTAINS(s, "[compacted]");
    free(s);
    turn_meta_end();
    turn_meta_free();
    return 0;
}
static int t8(void) {
    turn_meta_init();
    turn_meta_begin(1);
    turn_meta_set_latency(200);
    turn_meta_end();
    turn_meta_begin(2);
    turn_meta_set_latency(300);
    turn_meta_end();
    ASSERT(turn_meta_total_api_ms() == 500);
    char *s = turn_meta_format_summary();
    ASSERT_STR_CONTAINS(s, "2 turns");
    free(s);
    turn_meta_free();
    return 0;
}
static int t9(void) {
    turn_meta_init();
    char *s = turn_meta_format(-1);
    ASSERT_STR_CONTAINS(s, "Invalid");
    free(s);
    turn_meta_free();
    return 0;
}
static int ta(void) {
    turn_meta_init();
    turn_meta_set_tokens(1, 1);
    turn_meta_record_error();
    ASSERT_INT_EQ(turn_meta_count(), 0);
    turn_meta_free();
    return 0;
}
int test_turn_meta_all(void) {
    TEST_SUITE("turn_meta");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(ta);
    TEST_RETURN_FAILED();
}
