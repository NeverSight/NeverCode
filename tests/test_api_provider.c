#include "test.h"
#include "api.h"
#include "config.h"

static int test_provider_enum(void) {
    ASSERT_TRUE(PROVIDER_COMPAT != PROVIDER_NATIVE);
    ASSERT_INT_EQ(DEFAULT_PROVIDER, PROVIDER_COMPAT);
    return 0;
}

static int test_stream_dispatch(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT_INT_EQ(cfg.provider, DEFAULT_PROVIDER);
    config_free(&cfg);
    return 0;
}

static int test_provider_values(void) {
    ASSERT_GE(PROVIDER_COMPAT, 0);
    ASSERT_GE(PROVIDER_NATIVE, 0);
    ASSERT_TRUE(PROVIDER_COMPAT != PROVIDER_NATIVE);
    return 0;
}

int test_api_provider_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("api_provider");
    RUN_TEST(test_provider_enum);
    RUN_TEST(test_stream_dispatch);
    RUN_TEST(test_provider_values);
    TEST_RETURN_FAILED();
}
