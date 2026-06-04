#include "test.h"
#include "nevercode.h"

static int t_not_fired_initially(void) {
    api_preconnect_reset();
    ASSERT_FALSE(api_preconnect_fired());
    return 0;
}

static int t_fires_once(void) {
    api_preconnect_reset();
    api_preconnect("https://example.com");
    ASSERT_TRUE(api_preconnect_fired());
    api_preconnect("https://example.com");
    ASSERT_TRUE(api_preconnect_fired());
    return 0;
}

static int t_disabled(void) {
    api_preconnect_reset();
    api_preconnect_set_enabled(false);
    api_preconnect("https://example.com");
    ASSERT_FALSE(api_preconnect_fired());
    api_preconnect_set_enabled(true);
    return 0;
}

static int t_null_url(void) {
    api_preconnect_reset();
    api_preconnect_set_enabled(true);
    api_preconnect(NULL);
    ASSERT_FALSE(api_preconnect_fired());
    return 0;
}

static int t_empty_url(void) {
    api_preconnect_reset();
    api_preconnect_set_enabled(true);
    api_preconnect("");
    ASSERT_FALSE(api_preconnect_fired());
    return 0;
}

static int t_macros(void) {
    ASSERT_GT(PRECONN_TIMEOUT_SEC, 0);
    ASSERT_GT(PRECONN_CONNECT_SEC, 0);
    return 0;
}

int test_api_preconnect_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("api_preconnect");
    RUN_TEST(t_not_fired_initially);
    RUN_TEST(t_fires_once);
    RUN_TEST(t_disabled);
    RUN_TEST(t_null_url);
    RUN_TEST(t_empty_url);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
