#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int test_cf_init(void) {
    context_fatigue_init(128000);
    ASSERT_INT_EQ(context_fatigue_pct(), 0);
    ASSERT_INT_EQ(context_fatigue_turns_remaining(), -1);
    ASSERT_INT_EQ(context_fatigue_level(), 0);
    ASSERT_FALSE(context_fatigue_needs_compact());
    context_fatigue_free();
    return 0;
}

static int test_cf_record_and_pct(void) {
    context_fatigue_init(100000);

    context_fatigue_record(1, 10000);
    ASSERT_INT_EQ(context_fatigue_pct(), 10);

    context_fatigue_record(2, 30000);
    ASSERT_INT_EQ(context_fatigue_pct(), 30);

    context_fatigue_record(3, 70000);
    ASSERT_INT_EQ(context_fatigue_pct(), 70);

    context_fatigue_free();
    return 0;
}

static int test_cf_turns_remaining(void) {
    context_fatigue_init(100000);
    context_fatigue_record(1, 10000);
    context_fatigue_record(2, 20000);
    context_fatigue_record(3, 30000);

    int remaining = context_fatigue_turns_remaining();
    ASSERT_TRUE(remaining > 0);

    context_fatigue_free();
    return 0;
}

static int test_cf_level_zones(void) {
    context_fatigue_init(100000);

    context_fatigue_record(1, 50000);
    ASSERT_INT_EQ(context_fatigue_level(), 0);

    context_fatigue_record(2, 75000);
    ASSERT_INT_EQ(context_fatigue_level(), 1);

    context_fatigue_record(3, 90000);
    ASSERT_INT_EQ(context_fatigue_level(), 2);

    context_fatigue_record(4, 96000);
    ASSERT_INT_EQ(context_fatigue_level(), 3);

    context_fatigue_free();
    return 0;
}

static int test_cf_needs_compact(void) {
    context_fatigue_init(100000);

    context_fatigue_record(1, 50000);
    ASSERT_FALSE(context_fatigue_needs_compact());

    context_fatigue_record(2, 86000);
    ASSERT_TRUE(context_fatigue_needs_compact());

    context_fatigue_free();
    return 0;
}

static int test_cf_velocity(void) {
    context_fatigue_init(100000);
    context_fatigue_record(1, 5000);
    context_fatigue_record(2, 10000);
    context_fatigue_record(3, 15000);

    double vel = context_fatigue_velocity();
    ASSERT_TRUE(vel > 0.0);

    context_fatigue_free();
    return 0;
}

static int test_cf_format(void) {
    context_fatigue_init(100000);
    context_fatigue_record(1, 40000);

    char *out = context_fatigue_format();
    ASSERT_NOT_NULL(out);
    ASSERT_TRUE(strstr(out, "green") != NULL);
    free(out);

    context_fatigue_free();
    return 0;
}

static int test_cf_reset(void) {
    context_fatigue_init(100000);
    context_fatigue_record(1, 90000);
    ASSERT_TRUE(context_fatigue_needs_compact());

    context_fatigue_free();

    context_fatigue_init(100000);
    context_fatigue_record(1, 10000);
    ASSERT_FALSE(context_fatigue_needs_compact());

    context_fatigue_free();
    return 0;
}

static int test_cf_snapshot_restore(void) {
    context_fatigue_init(100000);
    context_fatigue_record(1, 50000);
    context_fatigue_record(2, 86000);

    context_fatigue_snapshot_t snap;
    context_fatigue_snapshot(&snap);
    ASSERT_INT_EQ(snap.count, 2);
    ASSERT_TRUE(snap.ema_velocity > 0.0);

    context_fatigue_free();
    context_fatigue_init(4096);
    context_fatigue_restore(&snap);

    ASSERT_INT_EQ(context_fatigue_pct(), 86);
    ASSERT_TRUE(context_fatigue_needs_compact());
    ASSERT_TRUE(context_fatigue_velocity() > 0.0);

    char *out = context_fatigue_format();
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "COMPACT RECOMMENDED");
    free(out);

    context_fatigue_free();
    return 0;
}

int test_context_fatigue_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("context_fatigue");

    RUN_TEST(test_cf_init);
    RUN_TEST(test_cf_record_and_pct);
    RUN_TEST(test_cf_turns_remaining);
    RUN_TEST(test_cf_level_zones);
    RUN_TEST(test_cf_needs_compact);
    RUN_TEST(test_cf_velocity);
    RUN_TEST(test_cf_format);
    RUN_TEST(test_cf_reset);
    RUN_TEST(test_cf_snapshot_restore);

    TEST_RETURN_FAILED();
}
