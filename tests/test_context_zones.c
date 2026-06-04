#include "test.h"
#include "nevercode.h"
#include "config.h"

typedef enum {
    CTX_ZONE_SAFE,
    CTX_ZONE_WARNING,
    CTX_ZONE_COMPACT,
    CTX_ZONE_BLOCKING
} context_zone_t;

static context_zone_t get_context_zone(int est, int limit) {
    if (limit <= 0)
        return CTX_ZONE_SAFE;
    int pct = est * 100 / limit;
    if (pct >= 95)
        return CTX_ZONE_BLOCKING;
    if (pct >= 90)
        return CTX_ZONE_COMPACT;
    if (pct >= 80)
        return CTX_ZONE_WARNING;
    return CTX_ZONE_SAFE;
}

static int test_zone_safe(void) {
    ASSERT_INT_EQ(get_context_zone(10000, 200000), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(100000, 200000), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(159000, 200000), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(0, 200000), CTX_ZONE_SAFE);
    return 0;
}

static int test_zone_warning(void) {
    ASSERT_INT_EQ(get_context_zone(160000, 200000), CTX_ZONE_WARNING);
    ASSERT_INT_EQ(get_context_zone(170000, 200000), CTX_ZONE_WARNING);
    ASSERT_INT_EQ(get_context_zone(179000, 200000), CTX_ZONE_WARNING);
    return 0;
}

static int test_zone_compact(void) {
    ASSERT_INT_EQ(get_context_zone(180000, 200000), CTX_ZONE_COMPACT);
    ASSERT_INT_EQ(get_context_zone(185000, 200000), CTX_ZONE_COMPACT);
    ASSERT_INT_EQ(get_context_zone(189000, 200000), CTX_ZONE_COMPACT);
    return 0;
}

static int test_zone_blocking(void) {
    ASSERT_INT_EQ(get_context_zone(190000, 200000), CTX_ZONE_BLOCKING);
    ASSERT_INT_EQ(get_context_zone(200000, 200000), CTX_ZONE_BLOCKING);
    ASSERT_INT_EQ(get_context_zone(250000, 200000), CTX_ZONE_BLOCKING);
    return 0;
}

static int test_zone_zero_limit(void) {
    ASSERT_INT_EQ(get_context_zone(10000, 0), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(0, 0), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(999999, -1), CTX_ZONE_SAFE);
    return 0;
}

static int test_zone_small_window(void) {
    ASSERT_INT_EQ(get_context_zone(700, 1000), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(800, 1000), CTX_ZONE_WARNING);
    ASSERT_INT_EQ(get_context_zone(900, 1000), CTX_ZONE_COMPACT);
    ASSERT_INT_EQ(get_context_zone(950, 1000), CTX_ZONE_BLOCKING);
    return 0;
}

static int test_zone_128k_model(void) {
    int limit = 128000;
    ASSERT_INT_EQ(get_context_zone(100000, limit), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(104000, limit), CTX_ZONE_WARNING);
    ASSERT_INT_EQ(get_context_zone(116000, limit), CTX_ZONE_COMPACT);
    ASSERT_INT_EQ(get_context_zone(122000, limit), CTX_ZONE_BLOCKING);
    return 0;
}

static int test_zone_256k_model(void) {
    int limit = 256000;
    ASSERT_INT_EQ(get_context_zone(200000, limit), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(210000, limit), CTX_ZONE_WARNING);
    ASSERT_INT_EQ(get_context_zone(235000, limit), CTX_ZONE_COMPACT);
    ASSERT_INT_EQ(get_context_zone(244000, limit), CTX_ZONE_BLOCKING);
    return 0;
}

static int test_config_cwd_init(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT_NULL(cfg.cwd);
    ASSERT_NULL(cfg.append_prompt);
    config_free(&cfg);
    return 0;
}

int test_context_zones_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("context_zones");
    RUN_TEST(test_zone_safe);
    RUN_TEST(test_zone_warning);
    RUN_TEST(test_zone_compact);
    RUN_TEST(test_zone_blocking);
    RUN_TEST(test_zone_zero_limit);
    RUN_TEST(test_zone_small_window);
    RUN_TEST(test_zone_128k_model);
    RUN_TEST(test_zone_256k_model);
    RUN_TEST(test_config_cwd_init);
    TEST_RETURN_FAILED();
}
