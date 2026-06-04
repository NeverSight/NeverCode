#include "test.h"
#include "nevercode.h"
#include "harness.h"
#include "config.h"
#include <string.h>

static int test_rca_null(void) {
    ASSERT_FALSE(remote_compact_available(NULL));
    return 0;
}

static int test_rca_empty_config(void) {
    config_t c;
    memset(&c, 0, sizeof(c));
    ASSERT_FALSE(remote_compact_available(&c));
    return 0;
}

static int test_rca_valid_config(void) {
    config_t c;
    memset(&c, 0, sizeof(c));
    c.provider = PROVIDER_COMPAT;
    c.compat_base_url = nc_xstrdup("https://api.example.com/v1");
    c.compat_api_key = nc_xstrdup("test-key");
    c.model = nc_xstrdup("test-model");
    ASSERT_TRUE(remote_compact_available(&c));
    free(c.compat_base_url);
    free(c.compat_api_key);
    free(c.model);
    return 0;
}

static int test_rcs_null_msgs(void) {
    ASSERT_NULL(remote_compact_summarize(NULL, 0, NULL, NULL, NULL, PROVIDER_COMPAT));
    return 0;
}

static int test_rcs_empty_msgs(void) {
    message_t m[1];
    memset(m, 0, sizeof(m));
    ASSERT_NULL(remote_compact_summarize(m, 0, "url", "key", "model", PROVIDER_COMPAT));
    return 0;
}

static int test_rca_missing_key(void) {
    config_t c;
    memset(&c, 0, sizeof(c));
    c.provider = PROVIDER_COMPAT;
    c.compat_base_url = nc_xstrdup("https://api.example.com/v1");
    c.model = nc_xstrdup("test-model");
    ASSERT_FALSE(remote_compact_available(&c));
    free(c.compat_base_url);
    free(c.model);
    return 0;
}

static int test_rca_missing_url(void) {
    config_t c;
    memset(&c, 0, sizeof(c));
    c.provider = PROVIDER_COMPAT;
    c.compat_api_key = nc_xstrdup("test-key");
    c.model = nc_xstrdup("test-model");
    ASSERT_FALSE(remote_compact_available(&c));
    free(c.compat_api_key);
    free(c.model);
    return 0;
}

int test_remote_compact_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("remote_compact");

    RUN_TEST(test_rca_null);
    RUN_TEST(test_rca_empty_config);
    RUN_TEST(test_rca_valid_config);
    RUN_TEST(test_rcs_null_msgs);
    RUN_TEST(test_rcs_empty_msgs);
    RUN_TEST(test_rca_missing_key);
    RUN_TEST(test_rca_missing_url);

    TEST_RETURN_FAILED();
}
