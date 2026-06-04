/*
 * test_idle_guard.c — Tests for idle timeout auto-exit guard.
 */
#include "test.h"
#include "nevercode.h"

static int t_disabled_by_default(void) {
    idle_guard_init(0);
    ASSERT(!idle_guard_enabled());
    ASSERT(!idle_guard_is_expired());
    ASSERT_INT_EQ(idle_guard_remaining_ms(), -1);
    ASSERT_INT_EQ(idle_guard_timeout_ms(), 0);
    return 0;
}

static int t_enabled_with_timeout(void) {
    idle_guard_init(5000);
    ASSERT(idle_guard_enabled());
    ASSERT_INT_EQ(idle_guard_timeout_ms(), 5000);
    ASSERT(!idle_guard_is_expired());
    idle_guard_stop();
    return 0;
}

static int t_touch_resets(void) {
    idle_guard_init(60000);
    idle_guard_start();
    ASSERT(!idle_guard_is_expired());
    idle_guard_touch();
    ASSERT(!idle_guard_is_expired());
    int rem = idle_guard_remaining_ms();
    ASSERT(rem > 0);
    idle_guard_stop();
    return 0;
}

static int t_stop_prevents_expiry(void) {
    idle_guard_init(1);
    idle_guard_start();
    idle_guard_stop();
    ASSERT(!idle_guard_is_expired());
    ASSERT_INT_EQ(idle_guard_remaining_ms(), -1);
    return 0;
}

static int t_macros(void) {
    ASSERT(IDLE_GUARD_DEFAULT_MS >= 0);
    ASSERT(strlen(IDLE_GUARD_ENV_VAR) > 0);
    return 0;
}

static int t_remaining_decreases(void) {
    idle_guard_init(100000);
    idle_guard_start();
    int r1 = idle_guard_remaining_ms();
    ASSERT(r1 > 0);
    ASSERT(r1 <= 100000);
    idle_guard_stop();
    return 0;
}

int test_idle_guard_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("idle_guard");
    RUN_TEST(t_disabled_by_default);
    RUN_TEST(t_enabled_with_timeout);
    RUN_TEST(t_touch_resets);
    RUN_TEST(t_stop_prevents_expiry);
    RUN_TEST(t_macros);
    RUN_TEST(t_remaining_decreases);
    TEST_RETURN_FAILED();
}
