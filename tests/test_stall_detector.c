#include "test.h"
#include "harness.h"
#include "nevercode.h"
#include <string.h>
#include <stdlib.h>

static int test_stall_init_ok(void) {
    stall_detector_init();
    ASSERT_INT_EQ(stall_detector_check(), STALL_OK);
    ASSERT_INT_EQ(stall_detector_window_count(), 0);
    stall_detector_free();
    return 0;
}

static int test_stall_single_turn_ok(void) {
    stall_detector_init();
    stall_detector_record_turn("Bash,Read", 500, true);
    ASSERT_INT_EQ(stall_detector_check(), STALL_OK);
    ASSERT_INT_EQ(stall_detector_window_count(), 1);
    stall_detector_free();
    return 0;
}

static int test_stall_loop_detected(void) {
    stall_detector_init();
    for (int i = 0; i < STALL_MIN_REPEAT + 1; i++)
        stall_detector_record_turn("Bash", 10, false);
    stall_verdict_t v = stall_detector_check();
    ASSERT(v == STALL_LOOP_DETECTED || v == STALL_NO_PROGRESS);
    stall_detector_free();
    return 0;
}

static int test_stall_progress_resets(void) {
    stall_detector_init();
    for (int i = 0; i < STALL_MIN_REPEAT; i++)
        stall_detector_record_turn("Bash", 10, false);
    stall_detector_record_turn("Write", 2000, true);
    ASSERT_INT_EQ(stall_detector_check(), STALL_OK);
    stall_detector_free();
    return 0;
}

static int test_stall_verdict_names(void) {
    ASSERT_STR_EQ(stall_verdict_name(STALL_OK), "ok");
    ASSERT_STR_EQ(stall_verdict_name(STALL_LOOP_DETECTED), "loop_detected");
    ASSERT_STR_EQ(stall_verdict_name(STALL_NO_PROGRESS), "no_progress");
    ASSERT_STR_EQ(stall_verdict_name(STALL_TIMEOUT), "timeout");
    return 0;
}

static int test_stall_suggest(void) {
    char *s = stall_detector_suggest(STALL_LOOP_DETECTED);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Stall");
    free(s);

    s = stall_detector_suggest(STALL_OK);
    ASSERT_NOT_NULL(s);
    free(s);
    return 0;
}

static int test_stall_window_overflow(void) {
    stall_detector_init();
    for (int i = 0; i < STALL_WINDOW_SIZE + 5; i++)
        stall_detector_record_turn("Read", i * 100, i % 3 == 0);
    ASSERT_INT_EQ(stall_detector_window_count(), STALL_WINDOW_SIZE);
    stall_detector_free();
    return 0;
}

static int test_stall_idle_tracking(void) {
    stall_detector_init();
    stall_detector_record_turn("Write", 500, true);
    long idle = stall_detector_idle_s();
    ASSERT(idle >= 0 && idle < 5);
    stall_detector_free();
    return 0;
}

static int test_stall_read_loop_basic(void) {
    stall_detector_reset_reads();
    ASSERT(!stall_detector_read_loop());
    stall_detector_record_read("/tmp/foo.c");
    stall_detector_record_read("/tmp/foo.c");
    stall_detector_record_read("/tmp/foo.c");
    ASSERT(stall_detector_read_loop());
    stall_detector_reset_reads();
    ASSERT(!stall_detector_read_loop());
    return 0;
}

static int test_stall_read_loop_distinct(void) {
    stall_detector_reset_reads();
    stall_detector_record_read("/a");
    stall_detector_record_read("/b");
    stall_detector_record_read("/c");
    ASSERT(!stall_detector_read_loop());
    stall_detector_reset_reads();
    return 0;
}

static int test_stall_snapshot_restore(void) {
    stall_detector_init();
    for (int i = 0; i < STALL_MIN_REPEAT; i++)
        stall_detector_record_turn("Read", 10, false);
    for (int i = 0; i < STALL_READ_LOOP_THRESHOLD; i++)
        stall_detector_record_read("/tmp/repeat.c");

    stall_snapshot_t snap;
    stall_detector_snapshot(&snap);
    ASSERT_INT_EQ(snap.window_count, STALL_MIN_REPEAT);
    ASSERT_INT_EQ(snap.read_freq_count, 1);

    stall_detector_free();
    stall_detector_restore(&snap);
    ASSERT_INT_EQ(stall_detector_window_count(), STALL_MIN_REPEAT);
    ASSERT(stall_detector_read_loop());
    ASSERT_INT_EQ(stall_detector_check(), STALL_LOOP_DETECTED);

    stall_detector_record_turn("Write", 400, true);
    ASSERT_INT_EQ(stall_detector_check(), STALL_OK);
    stall_detector_free();
    return 0;
}

int test_stall_detector_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("stall_detector");

    RUN_TEST(test_stall_init_ok);
    RUN_TEST(test_stall_single_turn_ok);
    RUN_TEST(test_stall_loop_detected);
    RUN_TEST(test_stall_progress_resets);
    RUN_TEST(test_stall_verdict_names);
    RUN_TEST(test_stall_suggest);
    RUN_TEST(test_stall_window_overflow);
    RUN_TEST(test_stall_idle_tracking);
    RUN_TEST(test_stall_read_loop_basic);
    RUN_TEST(test_stall_read_loop_distinct);
    RUN_TEST(test_stall_snapshot_restore);

    TEST_RETURN_FAILED();
}
