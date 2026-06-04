#include "test.h"
#include "nevercode.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_config_init(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT_INT_EQ(cfg.provider, DEFAULT_PROVIDER);
    ASSERT_INT_EQ(cfg.max_tokens, DEFAULT_MAX_TOKENS);
    ASSERT_NULL(cfg.model);
    ASSERT_NULL(cfg.initial_prompt);
    ASSERT_INT_EQ(cfg.verbose, false);
    ASSERT(cfg.trust_all_tools == true);
    config_free(&cfg);
    return 0;
}

static int test_config_model_defaults(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT_STR_EQ(config_model(NULL), nc_provider_default_model(DEFAULT_PROVIDER));
    cfg.provider = PROVIDER_NATIVE;
    const char *m1 = config_model(&cfg);
    ASSERT_NOT_NULL(m1);
    ASSERT_STR_EQ(m1, nc_provider_default_model(PROVIDER_NATIVE));

    cfg.provider = PROVIDER_COMPAT;
    const char *m2 = config_model(&cfg);
    ASSERT_NOT_NULL(m2);
    ASSERT_STR_EQ(m2, nc_provider_default_model(PROVIDER_COMPAT));
    config_free(&cfg);
    return 0;
}

static int test_provider_helpers(void) {
    provider_type_t provider = PROVIDER_COMPAT;
    ASSERT_STR_EQ(nc_provider_slug(PROVIDER_NATIVE), "native");
    ASSERT_STR_EQ(nc_provider_slug(PROVIDER_COMPAT), "compat");
    ASSERT_STR_EQ(nc_provider_label(PROVIDER_NATIVE), "Native API");
    ASSERT_STR_EQ(nc_provider_label(PROVIDER_COMPAT), "Compat API");
    ASSERT_STR_EQ(nc_provider_default_model(PROVIDER_NATIVE), DEFAULT_NATIVE_MODEL);
    ASSERT_STR_EQ(nc_provider_default_model(PROVIDER_COMPAT), DEFAULT_COMPAT_MODEL);
    ASSERT_STR_EQ(nc_provider_default_model(DEFAULT_PROVIDER), DEFAULT_COMPAT_MODEL);
    ASSERT_TRUE(nc_provider_parse("compat", &provider));
    ASSERT_INT_EQ((int)provider, (int)PROVIDER_COMPAT);
    ASSERT_TRUE(nc_provider_parse(PROVIDER_ALIAS_COMPAT, &provider));
    ASSERT_INT_EQ((int)provider, (int)PROVIDER_COMPAT);
    ASSERT_TRUE(nc_provider_parse("native", &provider));
    ASSERT_INT_EQ((int)provider, (int)PROVIDER_NATIVE);
    ASSERT_TRUE(nc_provider_parse(PROVIDER_ALIAS_NATIVE, &provider));
    ASSERT_INT_EQ((int)provider, (int)PROVIDER_NATIVE);
    ASSERT_FALSE(nc_provider_parse("unknown", &provider));
    return 0;
}

static int test_config_model_custom(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.model = nc_xstrdup("my-custom-model");
    ASSERT_STR_EQ(config_model(&cfg), "my-custom-model");
    config_free(&cfg);
    return 0;
}

static int test_config_api_key(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.native_api_key = nc_xstrdup("anth-key");
    cfg.compat_api_key = nc_xstrdup("oai-key");

    cfg.provider = PROVIDER_NATIVE;
    ASSERT_STR_EQ(config_api_key(&cfg), "anth-key");

    cfg.provider = PROVIDER_COMPAT;
    ASSERT_STR_EQ(config_api_key(&cfg), "oai-key");
    config_free(&cfg);
    return 0;
}

static int test_config_base_url(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.native_base_url = nc_xstrdup("https://api.anthropic.com");
    cfg.compat_base_url = nc_xstrdup("https://my-proxy.com/v1");

    cfg.provider = PROVIDER_NATIVE;
    ASSERT_STR_EQ(config_base_url(&cfg), "https://api.anthropic.com");

    cfg.provider = PROVIDER_COMPAT;
    ASSERT_STR_EQ(config_base_url(&cfg), "https://my-proxy.com/v1");
    config_free(&cfg);
    return 0;
}

static int test_config_free(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.model = nc_xstrdup("test");
    cfg.native_api_key = nc_xstrdup("key1");
    cfg.compat_api_key = nc_xstrdup("key2");
    cfg.native_base_url = nc_xstrdup("url1");
    cfg.compat_base_url = nc_xstrdup("url2");
    cfg.initial_prompt = nc_xstrdup("prompt");
    cfg.system_prompt_override = nc_xstrdup("sys");
    cfg.resume_session = nc_xstrdup("sid");
    config_free(&cfg);
    ASSERT_NULL(cfg.model);
    ASSERT_NULL(cfg.native_api_key);
    ASSERT_NULL(cfg.compat_api_key);
    return 0;
}

static int test_config_parse_help(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("fake-key");
    cfg.compat_base_url = nc_xstrdup("https://example.com/v1");
    char *argv[] = {NC_BINARY_NAME, "-m", "test-model", "-v", "-M"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.model, "test-model");
    ASSERT(cfg.verbose == true);
    ASSERT(cfg.no_markdown == true);
    config_free(&cfg);
    return 0;
}

static int test_config_parse_provider(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("test-key");
    char *argv[] = {NC_BINARY_NAME, "-P", PROVIDER_ALIAS_COMPAT, "-b", "https://example.com"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.provider, PROVIDER_COMPAT);
    ASSERT_NOT_NULL(cfg.compat_base_url);
    ASSERT(strstr(cfg.compat_base_url, "example.com") != NULL);
    ASSERT(strstr(cfg.compat_base_url, "/v1") != NULL);
    config_free(&cfg);
    return 0;
}

static int test_config_parse_prompt(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("fake-key");
    cfg.compat_base_url = nc_xstrdup("https://example.com/v1");
    char *argv[] = {NC_BINARY_NAME, "-p", "Hello world"};
    int rc = config_parse(&cfg, 3, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.initial_prompt, "Hello world");
    config_free(&cfg);
    return 0;
}

static int test_config_home_dir_priority(void) {
    const char *old_home = getenv(ENV_HOME);
    char *old_home_copy = old_home ? nc_xstrdup(old_home) : NULL;
    char tmpl[] = "/tmp/nevercode-test-home-XXXXXX";
    char *root = mkdtemp(tmpl);
    ASSERT_NOT_NULL(root);

    char primary[NC_PATH_MAX];
    char secondary[NC_PATH_MAX];
    snprintf(primary, sizeof(primary), "%s%s", root, CONFIG_DIR_PRIMARY);
    snprintf(secondary, sizeof(secondary), "%s%s", root, CONFIG_DIR_SECONDARY);
    ASSERT_INT_EQ(mkdir(secondary, NC_DIR_MODE), 0);
    ASSERT_INT_EQ(mkdir(primary, NC_DIR_MODE), 0);

    ASSERT_INT_EQ(setenv("HOME", root, 1), 0);
    config_home_reset_cache();
    ASSERT_STR_EQ(config_home_dir(), primary);

    ASSERT_INT_EQ(rmdir(primary), 0);
    ASSERT_INT_EQ(rmdir(secondary), 0);
    ASSERT_INT_EQ(rmdir(root), 0);
    if (old_home_copy) {
        ASSERT_INT_EQ(setenv("HOME", old_home_copy, 1), 0);
        free(old_home_copy);
    } else {
        ASSERT_INT_EQ(unsetenv("HOME"), 0);
    }
    config_home_reset_cache();
    return 0;
}

static int test_config_home_dir_no_home_fallback(void) {
    const char *old_home = getenv(ENV_HOME);
    char *old_home_copy = old_home ? nc_xstrdup(old_home) : NULL;
    ASSERT_INT_EQ(unsetenv("HOME"), 0);
    config_home_reset_cache();
    ASSERT_STR_EQ(config_home_dir(), "/tmp" CONFIG_DIR_PRIMARY);
    if (old_home_copy) {
        ASSERT_INT_EQ(setenv("HOME", old_home_copy, 1), 0);
        free(old_home_copy);
    }
    config_home_reset_cache();
    return 0;
}

int test_config_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("config");
    RUN_TEST(test_config_init);
    RUN_TEST(test_config_model_defaults);
    RUN_TEST(test_provider_helpers);
    RUN_TEST(test_config_model_custom);
    RUN_TEST(test_config_api_key);
    RUN_TEST(test_config_base_url);
    RUN_TEST(test_config_free);
    RUN_TEST(test_config_parse_help);
    RUN_TEST(test_config_parse_provider);
    RUN_TEST(test_config_parse_prompt);
    RUN_TEST(test_config_home_dir_priority);
    RUN_TEST(test_config_home_dir_no_home_fallback);
    TEST_RETURN_FAILED();
}
