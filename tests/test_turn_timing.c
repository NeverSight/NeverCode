#include "test.h"
#include "nevercode.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int test_timing_init(void) {
    turn_timing_init();
    ASSERT_INT_EQ(turn_timing_count(), 0);
    return 0;
}

static int test_timing_record(void) {
    turn_timing_init();
    turn_timing_record(1, 500, 200, 3, 1000, 500);
    ASSERT_INT_EQ(turn_timing_count(), 1);
    return 0;
}

static int test_timing_multi_record(void) {
    turn_timing_init();
    turn_timing_record(1, 500, 200, 3, 1000, 500);
    turn_timing_record(2, 1000, 800, 5, 2000, 1000);
    turn_timing_record(3, 300, 100, 1, 500, 200);
    ASSERT_INT_EQ(turn_timing_count(), 3);
    return 0;
}

static int test_timing_format_empty(void) {
    turn_timing_init();
    char *report = turn_timing_format();
    ASSERT_NOT_NULL(report);
    ASSERT(strstr(report, "No turn timing") != NULL);
    free(report);
    return 0;
}

static int test_timing_format_with_data(void) {
    turn_timing_init();
    turn_timing_record(1, 500, 200, 3, 1000, 500);
    turn_timing_record(2, 1000, 800, 5, 2000, 1000);
    char *report = turn_timing_format();
    ASSERT_NOT_NULL(report);
    ASSERT(strstr(report, "Turn Timing Report") != NULL);
    ASSERT(strstr(report, "Total") != NULL);
    ASSERT(strstr(report, "Avg") != NULL);
    free(report);
    return 0;
}

static int test_timing_total_api_ms(void) {
    turn_timing_init();
    turn_timing_record(1, 500, 200, 3, 1000, 500);
    turn_timing_record(2, 1000, 800, 5, 2000, 1000);
    ASSERT(turn_timing_total_api_ms() == 1500);
    return 0;
}

static int test_timing_total_tool_ms(void) {
    turn_timing_init();
    turn_timing_record(1, 500, 200, 3, 1000, 500);
    turn_timing_record(2, 1000, 800, 5, 2000, 1000);
    ASSERT(turn_timing_total_tool_ms() == 1000);
    return 0;
}

static int test_timing_elapsed_ms(void) {
    turn_timing_init();
    turn_timing_start();
    usleep(10000); /* 10ms */
    long elapsed = turn_timing_elapsed_ms();
    ASSERT(elapsed >= 5);
    ASSERT(elapsed < 1000);
    return 0;
}

static int test_timing_start_reset(void) {
    turn_timing_init();
    turn_timing_start();
    usleep(20000);
    long e1 = turn_timing_elapsed_ms();
    turn_timing_start();
    usleep(5000);
    long e2 = turn_timing_elapsed_ms();
    ASSERT(e2 < e1);
    return 0;
}

static int test_timing_record_ex_ttft(void) {
    turn_timing_init();
    turn_timing_record_ex(1, 800, 300, 120, 2, 1500, 700);
    ASSERT_INT_EQ(turn_timing_count(), 1);
    char *report = turn_timing_format();
    ASSERT_NOT_NULL(report);
    ASSERT(strstr(report, "TTFT") != NULL);
    ASSERT(strstr(report, "120") != NULL);
    free(report);
    return 0;
}

static int test_timing_ttft_no_value(void) {
    turn_timing_init();
    turn_timing_record_ex(1, 500, 200, -1, 1, 1000, 500);
    char *report = turn_timing_format();
    ASSERT_NOT_NULL(report);
    ASSERT(strstr(report, "n/a") != NULL);
    free(report);
    return 0;
}

static int test_timing_ttft_mixed(void) {
    turn_timing_init();
    turn_timing_record_ex(1, 500, 200, 90, 1, 1000, 500);
    turn_timing_record_ex(2, 800, 300, -1, 2, 2000, 1000);
    turn_timing_record_ex(3, 600, 100, 110, 1, 800, 400);
    ASSERT_INT_EQ(turn_timing_count(), 3);
    char *report = turn_timing_format();
    ASSERT_NOT_NULL(report);
    ASSERT(strstr(report, "Avg TTFT") != NULL);
    ASSERT(strstr(report, "n/a") != NULL);
    free(report);
    return 0;
}

static int test_timing_record_legacy_no_ttft(void) {
    turn_timing_init();
    turn_timing_record(1, 500, 200, 3, 1000, 500);
    char *report = turn_timing_format();
    ASSERT_NOT_NULL(report);
    ASSERT(strstr(report, "n/a") != NULL);
    free(report);
    return 0;
}

int test_turn_timing_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("turn_timing");

    RUN_TEST(test_timing_init);
    RUN_TEST(test_timing_record);
    RUN_TEST(test_timing_multi_record);
    RUN_TEST(test_timing_format_empty);
    RUN_TEST(test_timing_format_with_data);
    RUN_TEST(test_timing_total_api_ms);
    RUN_TEST(test_timing_total_tool_ms);
    RUN_TEST(test_timing_elapsed_ms);
    RUN_TEST(test_timing_start_reset);
    RUN_TEST(test_timing_record_ex_ttft);
    RUN_TEST(test_timing_ttft_no_value);
    RUN_TEST(test_timing_ttft_mixed);
    RUN_TEST(test_timing_record_legacy_no_ttft);

    TEST_RETURN_FAILED();
}
