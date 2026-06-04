/*
 * test_sleep_inhibit.c — Cross-platform idle-sleep prevention tests.
 */

#include "test.h"
#include "nevercode.h"
#include <stdlib.h>

static int test_toggle(void) {
    sleep_inhibit_set_enabled(true);
    sleep_inhibit_set_turn(true);
    ASSERT(sleep_inhibit_is_turn_running());
    sleep_inhibit_set_turn(false);
    ASSERT(!sleep_inhibit_is_turn_running());
    return 0;
}

static int test_disabled_prevents_assertion(void) {
    sleep_inhibit_set_enabled(false);
    sleep_inhibit_set_turn(true);
    ASSERT(sleep_inhibit_is_turn_running());
    ASSERT(!sleep_inhibit_active());
    sleep_inhibit_set_turn(false);
    sleep_inhibit_set_enabled(true);
    return 0;
}

static int test_idempotent(void) {
    sleep_inhibit_set_enabled(true);
    sleep_inhibit_set_turn(true);
    sleep_inhibit_set_turn(true);
    sleep_inhibit_set_turn(true);
    sleep_inhibit_set_turn(false);
    ASSERT(!sleep_inhibit_is_turn_running());
    return 0;
}

static int test_multiple_cycles(void) {
    sleep_inhibit_set_enabled(true);
    for (int i = 0; i < 5; i++) {
        sleep_inhibit_set_turn(true);
        ASSERT(sleep_inhibit_is_turn_running());
        sleep_inhibit_set_turn(false);
        ASSERT(!sleep_inhibit_is_turn_running());
    }
    return 0;
}

static int test_acquire_release_roundtrip(void) {
    sleep_inhibit_acquire();
#if defined(__APPLE__) || defined(__linux__)
    ASSERT(sleep_inhibit_active());
#endif
    sleep_inhibit_release();
    ASSERT(!sleep_inhibit_active());
    return 0;
}

static int test_double_acquire(void) {
    sleep_inhibit_acquire();
    sleep_inhibit_acquire();
#if defined(__APPLE__) || defined(__linux__)
    ASSERT(sleep_inhibit_active());
#endif
    sleep_inhibit_release();
    ASSERT(!sleep_inhibit_active());
    return 0;
}

static int test_release_without_acquire(void) {
    sleep_inhibit_release();
    ASSERT(!sleep_inhibit_active());
    return 0;
}

int test_sleep_inhibit_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("sleep_inhibit");
    RUN_TEST(test_toggle);
    RUN_TEST(test_disabled_prevents_assertion);
    RUN_TEST(test_idempotent);
    RUN_TEST(test_multiple_cycles);
    RUN_TEST(test_acquire_release_roundtrip);
    RUN_TEST(test_double_acquire);
    RUN_TEST(test_release_without_acquire);
    TEST_RETURN_FAILED();
}
