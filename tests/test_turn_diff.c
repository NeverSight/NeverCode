#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_turn_diff_init(void) {
    turn_diff_init();
    ASSERT_INT_EQ(turn_diff_total_added(), 0);
    ASSERT_INT_EQ(turn_diff_total_removed(), 0);
    turn_diff_free();
    return 0;
}

static int test_turn_diff_record(void) {
    turn_diff_init();
    turn_diff_record(1, "src/main.c", 10, 3);
    ASSERT_INT_EQ(turn_diff_total_added(), 10);
    ASSERT_INT_EQ(turn_diff_total_removed(), 3);
    turn_diff_free();
    return 0;
}

static int test_turn_diff_merge(void) {
    turn_diff_init();
    turn_diff_record(1, "src/main.c", 5, 2);
    turn_diff_record(1, "src/main.c", 3, 1);
    ASSERT_INT_EQ(turn_diff_total_added(), 8);
    ASSERT_INT_EQ(turn_diff_total_removed(), 3);
    turn_diff_free();
    return 0;
}

static int test_turn_diff_multi_file(void) {
    turn_diff_init();
    turn_diff_record(1, "a.c", 10, 0);
    turn_diff_record(1, "b.c", 5, 5);
    turn_diff_record(2, "c.c", 20, 10);
    ASSERT_INT_EQ(turn_diff_total_added(), 35);
    ASSERT_INT_EQ(turn_diff_total_removed(), 15);
    turn_diff_free();
    return 0;
}

static int test_turn_diff_format_single(void) {
    turn_diff_init();
    turn_diff_record(1, "src/test.c", 7, 2);
    char *s = turn_diff_format(1);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "src/test.c") != NULL);
    ASSERT(strstr(s, "+7") != NULL);
    ASSERT(strstr(s, "-2") != NULL);
    free(s);
    turn_diff_free();
    return 0;
}

static int test_turn_diff_format_empty(void) {
    turn_diff_init();
    char *s = turn_diff_format(99);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "no file changes") != NULL);
    free(s);
    turn_diff_free();
    return 0;
}

static int test_turn_diff_format_all(void) {
    turn_diff_init();
    turn_diff_record(1, "a.c", 10, 0);
    turn_diff_record(2, "b.c", 5, 3);
    char *s = turn_diff_format_all();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "Turn 1") != NULL);
    ASSERT(strstr(s, "Turn 2") != NULL);
    ASSERT(strstr(s, "Total:") != NULL);
    ASSERT(strstr(s, "+15") != NULL);
    free(s);
    turn_diff_free();
    return 0;
}

static int test_turn_diff_format_all_empty(void) {
    turn_diff_init();
    char *s = turn_diff_format_all();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "No file changes") != NULL);
    free(s);
    turn_diff_free();
    return 0;
}

static int test_turn_diff_null_path(void) {
    turn_diff_init();
    turn_diff_record(1, NULL, 5, 0);
    ASSERT_INT_EQ(turn_diff_total_added(), 0);
    turn_diff_free();
    return 0;
}

int test_turn_diff_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("turn_diff");

    RUN_TEST(test_turn_diff_init);
    RUN_TEST(test_turn_diff_record);
    RUN_TEST(test_turn_diff_merge);
    RUN_TEST(test_turn_diff_multi_file);
    RUN_TEST(test_turn_diff_format_single);
    RUN_TEST(test_turn_diff_format_empty);
    RUN_TEST(test_turn_diff_format_all);
    RUN_TEST(test_turn_diff_format_all_empty);
    RUN_TEST(test_turn_diff_null_path);

    TEST_RETURN_FAILED();
}
