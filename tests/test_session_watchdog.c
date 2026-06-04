/*
 * test_session_watchdog.c — Tests for session watchdog / per-turn timeout guard
 */

#include "test.h"
#include "harness.h"
#include <unistd.h>

static int test_init_defaults(void) {
    watchdog_init(0);
    ASSERT_INT_EQ(watchdog_timeout_count(), 0);
    ASSERT_INT_EQ(watchdog_consecutive_timeouts(), 0);
    ASSERT(watchdog_longest_turn_ms() == 0);
    watchdog_free();
    return 0;
}

static int test_begin_end_turn(void) {
    watchdog_init(5000);
    watchdog_begin_turn(1);
    watchdog_verdict_t v = watchdog_check();
    ASSERT_INT_EQ(v, WD_VERDICT_OK);
    watchdog_end_turn(true);
    ASSERT_INT_EQ(watchdog_timeout_count(), 0);
    ASSERT_INT_EQ(watchdog_consecutive_timeouts(), 0);
    watchdog_free();
    return 0;
}

static int test_elapsed_while_running(void) {
    watchdog_init(10000);
    watchdog_begin_turn(1);
    usleep(10000);
    long el = watchdog_elapsed_ms();
    ASSERT(el >= 5);
    watchdog_end_turn(true);

    long after = watchdog_elapsed_ms();
    ASSERT_INT_EQ((int)after, 0);
    watchdog_free();
    return 0;
}

static int test_warn_threshold(void) {
    watchdog_init(100);
    watchdog_begin_turn(1);
    usleep(85000);
    watchdog_verdict_t v = watchdog_check();
    ASSERT(v == WD_VERDICT_WARN || v == WD_VERDICT_TIMEOUT);
    watchdog_end_turn(true);
    watchdog_free();
    return 0;
}

static int test_timeout_fires(void) {
    watchdog_init(50);
    watchdog_begin_turn(1);
    usleep(60000);
    watchdog_verdict_t v = watchdog_check();
    ASSERT(v == WD_VERDICT_TIMEOUT || v == WD_VERDICT_HANDOFF);
    ASSERT_INT_EQ(watchdog_timeout_count(), 1);
    watchdog_end_turn(false);
    watchdog_free();
    return 0;
}

static int test_consecutive_timeouts_trigger_handoff(void) {
    watchdog_init(20);
    for (int i = 0; i < WATCHDOG_HANDOFF_THRESHOLD; i++) {
        watchdog_begin_turn(i + 1);
        usleep(30000);
        (void)watchdog_check();
        watchdog_end_turn(false);
    }
    watchdog_begin_turn(WATCHDOG_HANDOFF_THRESHOLD + 1);
    usleep(30000);
    watchdog_verdict_t v = watchdog_check();
    ASSERT_INT_EQ(v, WD_VERDICT_HANDOFF);
    watchdog_end_turn(false);
    watchdog_free();
    return 0;
}

static int test_success_resets_consecutive(void) {
    watchdog_init(20);
    watchdog_begin_turn(1);
    usleep(30000);
    (void)watchdog_check();
    watchdog_end_turn(false);
    ASSERT_INT_EQ(watchdog_consecutive_timeouts(), 1);

    watchdog_begin_turn(2);
    watchdog_end_turn(true);
    ASSERT_INT_EQ(watchdog_consecutive_timeouts(), 0);
    ASSERT_INT_EQ(watchdog_timeout_count(), 1);
    watchdog_free();
    return 0;
}

static int test_longest_turn_tracking(void) {
    watchdog_init(5000);
    watchdog_begin_turn(1);
    usleep(10000);
    watchdog_end_turn(true);

    watchdog_begin_turn(2);
    usleep(30000);
    watchdog_end_turn(true);

    ASSERT(watchdog_longest_turn_ms() >= 20);
    watchdog_free();
    return 0;
}

static int test_set_deadline(void) {
    watchdog_init(100);
    watchdog_set_deadline(50);
    watchdog_begin_turn(1);
    usleep(60000);
    watchdog_verdict_t v = watchdog_check();
    ASSERT(v == WD_VERDICT_TIMEOUT || v == WD_VERDICT_HANDOFF);
    watchdog_end_turn(false);
    watchdog_free();
    return 0;
}

static int test_stream_xfer_poll_abort(void) {
    watchdog_init(25);
    watchdog_begin_turn(99);
    usleep(40000);
    ASSERT_INT_EQ(watchdog_stream_xfer_poll(), 1);
    ASSERT_INT_EQ(watchdog_stream_xfer_take_verdict(), WD_VERDICT_TIMEOUT);
    ASSERT_INT_EQ(watchdog_stream_xfer_take_verdict(), WD_VERDICT_OK);
    watchdog_end_turn(false);
    watchdog_free();
    return 0;
}

static int test_stream_xfer_poll_ok_before_deadline(void) {
    watchdog_init(60000);
    watchdog_begin_turn(1);
    ASSERT_INT_EQ(watchdog_stream_xfer_poll(), 0);
    ASSERT_INT_EQ(watchdog_stream_xfer_take_verdict(), WD_VERDICT_OK);
    watchdog_end_turn(true);
    watchdog_free();
    return 0;
}

static int test_recovery_prompt_timeout(void) {
    char *p = watchdog_recovery_prompt(WD_VERDICT_TIMEOUT);
    ASSERT_NOT_NULL(p);
    ASSERT(strlen(p) > 0);
    ASSERT_STR_CONTAINS(p, "timed out");
    free(p);
    return 0;
}

static int test_recovery_prompt_handoff(void) {
    char *p = watchdog_recovery_prompt(WD_VERDICT_HANDOFF);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "timed out");
    free(p);
    return 0;
}

static int test_recovery_prompt_ok(void) {
    char *p = watchdog_recovery_prompt(WD_VERDICT_OK);
    ASSERT_NOT_NULL(p);
    ASSERT_INT_EQ((int)strlen(p), 0);
    free(p);
    return 0;
}

static int test_format_status(void) {
    watchdog_init(5000);
    watchdog_begin_turn(1);
    char *s = watchdog_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "running");
    ASSERT_STR_CONTAINS(s, "deadline");
    free(s);
    watchdog_end_turn(true);

    s = watchdog_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "idle");
    free(s);
    watchdog_free();
    return 0;
}

static int test_snapshot_restore(void) {
    watchdog_init(20);
    watchdog_begin_turn(1);
    usleep(30000);
    (void)watchdog_check();
    watchdog_end_turn(false);

    watchdog_snapshot_t snap;
    watchdog_snapshot(&snap);
    watchdog_free();

    watchdog_init(5000);
    watchdog_restore(&snap);
    ASSERT_INT_EQ(watchdog_timeout_count(), 1);
    ASSERT_INT_EQ(watchdog_consecutive_timeouts(), 1);
    ASSERT(watchdog_longest_turn_ms() >= 20);

    char *s = watchdog_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "deadline     20 ms");
    ASSERT_STR_CONTAINS(s, "consecutive  1 timeouts");
    free(s);

    watchdog_begin_turn(2);
    watchdog_end_turn(true);
    ASSERT_INT_EQ(watchdog_consecutive_timeouts(), 0);
    watchdog_free();
    return 0;
}

int test_session_watchdog_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_watchdog");
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_begin_end_turn);
    RUN_TEST(test_elapsed_while_running);
    RUN_TEST(test_warn_threshold);
    RUN_TEST(test_timeout_fires);
    RUN_TEST(test_consecutive_timeouts_trigger_handoff);
    RUN_TEST(test_success_resets_consecutive);
    RUN_TEST(test_longest_turn_tracking);
    RUN_TEST(test_set_deadline);
    RUN_TEST(test_stream_xfer_poll_abort);
    RUN_TEST(test_stream_xfer_poll_ok_before_deadline);
    RUN_TEST(test_recovery_prompt_timeout);
    RUN_TEST(test_recovery_prompt_handoff);
    RUN_TEST(test_recovery_prompt_ok);
    RUN_TEST(test_format_status);
    RUN_TEST(test_snapshot_restore);
    TEST_RETURN_FAILED();
}
