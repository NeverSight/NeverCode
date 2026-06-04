#include "test.h"
#include "nevercode.h"
static int t1(void) {
    auto_compact_init(128000);
    ASSERT_TRUE(auto_compact_enabled());
    ASSERT_INT_EQ(auto_compact_total(), 0);
    int ew = auto_compact_effective_window();
    ASSERT(ew > 0);
    ASSERT(ew < 128000);
    auto_compact_free();
    return 0;
}
static int t2(void) {
    auto_compact_init(100000);
    ASSERT(auto_compact_usage_pct(50000) > 0);
    ASSERT_INT_EQ(auto_compact_usage_pct(0), 0);
    auto_compact_free();
    return 0;
}
static int t3(void) {
    auto_compact_init(100000);
    ASSERT_FALSE(auto_compact_should_trigger(10000));
    ASSERT_TRUE(
        auto_compact_should_trigger((int)((long)auto_compact_effective_window() * 90 / 100)));
    auto_compact_free();
    return 0;
}
static int t4(void) {
    auto_compact_init(200000);
    auto_compact_set_enabled(false);
    ASSERT_FALSE(auto_compact_should_trigger(999999));
    auto_compact_set_enabled(true);
    auto_compact_free();
    return 0;
}
static int t5(void) {
    auto_compact_init(128000);
    auto_compact_set_window(200000, 20000);
    ASSERT_INT_EQ(auto_compact_effective_window(), 180000);
    auto_compact_free();
    return 0;
}
static int t6(void) {
    auto_compact_init(128000);
    char *s = auto_compact_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Auto-Compact");
    free(s);
    auto_compact_free();
    return 0;
}
static int t7_circuit_breaker(void) {
    auto_compact_init(100000);
    ASSERT_INT_EQ(auto_compact_circuit_trips(), 0);

    int high = (int)((long)auto_compact_effective_window() * 90 / 100);
    ASSERT_TRUE(auto_compact_should_trigger(high));

    for (int i = 0; i < CIRCUIT_BREAKER_THRESHOLD; i++)
        auto_compact_record_failure();
    ASSERT_FALSE(auto_compact_should_trigger(high));
    ASSERT_INT_EQ(auto_compact_circuit_trips(), 1);

    auto_compact_reset_circuit();
    ASSERT_TRUE(auto_compact_should_trigger(high));
    ASSERT_INT_EQ(auto_compact_circuit_trips(), 1);

    auto_compact_free();
    return 0;
}
static int t8_reserve_capping(void) {
    auto_compact_init(200000);
    int ew = auto_compact_effective_window();
    int expected_reserve = DEFAULT_MAX_TOKENS < AUTO_COMPACT_RESERVE_TOKENS
                               ? DEFAULT_MAX_TOKENS
                               : AUTO_COMPACT_RESERVE_TOKENS;
    ASSERT_INT_EQ(ew, 200000 - expected_reserve);

    auto_compact_set_window(200000, 50000);
    ASSERT_INT_EQ(auto_compact_effective_window(), 200000 - AUTO_COMPACT_RESERVE_TOKENS);

    auto_compact_set_window(200000, 5000);
    ASSERT_INT_EQ(auto_compact_effective_window(), 200000 - 5000);

    auto_compact_free();
    return 0;
}
static int t9_overflow_detection(void) {
    auto_compact_init(200000);
    ASSERT_FALSE(auto_compact_is_overflow(100000, DEFAULT_MAX_TOKENS));
    ASSERT_TRUE(auto_compact_is_overflow(195000, DEFAULT_MAX_TOKENS));
    auto_compact_free();
    return 0;
}
static int t10_input_limit_override(void) {
    auto_compact_init(200000);
    auto_compact_set_input_limit(80000);
    ASSERT_TRUE(auto_compact_is_overflow(85000, DEFAULT_MAX_TOKENS));
    auto_compact_free();
    return 0;
}
static int t11_degradation_warning(void) {
    auto_compact_init(128000);
    ASSERT_FALSE(auto_compact_should_warn_degradation());
    ASSERT_INT_EQ(auto_compact_full_count(), 0);
    auto_compact_free();
    return 0;
}
static int t12_mid_turn_prefix(void) {
    compact_tpl_init();
    const char *p = compact_tpl_mid_turn_prefix();
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "compacted");
    compact_tpl_free();
    return 0;
}
int test_auto_compact_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("auto_compact");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7_circuit_breaker);
    RUN_TEST(t8_reserve_capping);
    RUN_TEST(t9_overflow_detection);
    RUN_TEST(t10_input_limit_override);
    RUN_TEST(t11_degradation_warning);
    RUN_TEST(t12_mid_turn_prefix);
    TEST_RETURN_FAILED();
}
