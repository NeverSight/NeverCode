#include "test.h"
#include "nevercode.h"
#include "config.h"
#include <stdlib.h>

static int test_url_bare_domain(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("test-key");
    char *argv[] = {NC_BINARY_NAME, "-P", "openai", "-b", "https://compat-gateway.test"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.compat_base_url, "https://compat-gateway.test/v1");
    config_free(&cfg);
    return 0;
}

static int test_url_trailing_slash(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("test-key");
    char *argv[] = {NC_BINARY_NAME, "-P", "openai", "-b", "https://example.com/"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.compat_base_url, "https://example.com/v1");
    config_free(&cfg);
    return 0;
}

static int test_url_with_v1(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("test-key");
    char *argv[] = {NC_BINARY_NAME, "-P", "openai", "-b", "https://example.com/v1"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.compat_base_url, "https://example.com/v1");
    config_free(&cfg);
    return 0;
}

static int test_url_with_v1_slash(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("test-key");
    char *argv[] = {NC_BINARY_NAME, "-P", "openai", "-b", "https://example.com/v1/"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.compat_base_url, "https://example.com/v1");
    config_free(&cfg);
    return 0;
}

static int test_url_full_endpoint(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("test-key");
    char *argv[] = {NC_BINARY_NAME, "-P", "openai", "-b",
                    "https://example.com/v1/chat/completions"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.compat_base_url, "https://example.com/v1");
    config_free(&cfg);
    return 0;
}

static int test_url_custom_prefix(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("test-key");
    char *argv[] = {NC_BINARY_NAME, "-P", "openai", "-b", "https://proxy.example.com/custom/v1"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.compat_base_url, "https://proxy.example.com/custom/v1");
    config_free(&cfg);
    return 0;
}

static int test_url_localhost(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("test-key");
    char *argv[] = {NC_BINARY_NAME, "-P", "openai", "-b", "http://127.0.0.1:11434"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.compat_base_url, "http://127.0.0.1:11434/v1");
    config_free(&cfg);
    return 0;
}

static int test_base_url_auto_switch_provider(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("test-key");
    char *argv[] = {NC_BINARY_NAME, "-b", "https://example.com"};
    int rc = config_parse(&cfg, 3, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.provider, PROVIDER_COMPAT);
    config_free(&cfg);
    return 0;
}

static int test_env_custom_gateway_and_model(void) {
    const char *saved_key = getenv(ENV_COMPAT_KEY);
    const char *saved_url = getenv(ENV_COMPAT_URL);
    char *prev_key = saved_key ? nc_xstrdup(saved_key) : NULL;
    char *prev_url = saved_url ? nc_xstrdup(saved_url) : NULL;

    config_t cfg;
    config_init(&cfg);
    setenv("OPENAI_API_KEY", "test-key", 1);
    setenv("OPENAI_BASE_URL", "https://compat-gateway.test", 1);

    char *argv[] = {NC_BINARY_NAME, "-m", "gpt-5.4"};
    int rc = config_parse(&cfg, 3, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.provider, PROVIDER_COMPAT);
    ASSERT_STR_EQ(cfg.compat_base_url, "https://compat-gateway.test/v1");
    ASSERT_STR_EQ(config_model(&cfg), "gpt-5.4");

    if (prev_key)
        setenv("OPENAI_API_KEY", prev_key, 1);
    else
        unsetenv("OPENAI_API_KEY");
    if (prev_url)
        setenv("OPENAI_BASE_URL", prev_url, 1);
    else
        unsetenv("OPENAI_BASE_URL");
    free(prev_key);
    free(prev_url);
    config_free(&cfg);
    return 0;
}

int test_url_normalize_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("url_normalize");
    RUN_TEST(test_url_bare_domain);
    RUN_TEST(test_url_trailing_slash);
    RUN_TEST(test_url_with_v1);
    RUN_TEST(test_url_with_v1_slash);
    RUN_TEST(test_url_full_endpoint);
    RUN_TEST(test_url_custom_prefix);
    RUN_TEST(test_url_localhost);
    RUN_TEST(test_base_url_auto_switch_provider);
    RUN_TEST(test_env_custom_gateway_and_model);
    TEST_RETURN_FAILED();
}
