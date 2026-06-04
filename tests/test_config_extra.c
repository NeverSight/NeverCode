#include "test.h"
#include "nevercode.h"
#include "config.h"
#include <stdio.h>
#include <unistd.h>

static int test_config_append_system_prompt_file(void) {
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "/tmp/nevercode_test_append_%d.txt", (int)getpid());
    FILE *fp = fopen(path, "w");
    ASSERT_NOT_NULL(fp);
    fputs("from_file", fp);
    fclose(fp);

    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("fake-key");
    cfg.compat_base_url = nc_xstrdup("https://example.com/v1");
    char *argv[] = {NC_BINARY_NAME, "-f", path, "-A", "from_cli"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(cfg.append_prompt);
    ASSERT_STR_CONTAINS(cfg.append_prompt, "from_file");
    ASSERT_STR_CONTAINS(cfg.append_prompt, "from_cli");
    config_free(&cfg);
    remove(path);
    return 0;
}

static int test_config_json_flag(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("fake-key");
    cfg.compat_base_url = nc_xstrdup("https://example.com/v1");
    char *argv[] = {NC_BINARY_NAME, "-j", "-p", "hello"};
    int rc = config_parse(&cfg, 4, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(cfg.json_output == true);
    ASSERT_STR_EQ(cfg.initial_prompt, "hello");
    config_free(&cfg);
    return 0;
}

static int test_config_output_flag(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("fake-key");
    cfg.compat_base_url = nc_xstrdup("https://example.com/v1");
    char *argv[] = {NC_BINARY_NAME, "-o", "/tmp/out.txt", "-p", "test"};
    int rc = config_parse(&cfg, 5, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.output_file, "/tmp/out.txt");
    config_free(&cfg);
    return 0;
}

static int test_config_json_default_false(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT(cfg.json_output == false);
    ASSERT_NULL(cfg.output_file);
    config_free(&cfg);
    return 0;
}

static int test_config_combined_flags(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("key");
    char *argv[] = {NC_BINARY_NAME, "-P",      "openai", "-b", "https://example.com",
                    "-m",           "gpt-5.4", "-j",     "-o", "/tmp/r.txt",
                    "-D",           "-p",      "test"};
    int rc = config_parse(&cfg, 13, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.provider, PROVIDER_COMPAT);
    ASSERT(cfg.json_output == true);
    ASSERT_STR_EQ(cfg.output_file, "/tmp/r.txt");
    ASSERT(cfg.trust_all_tools == true);
    ASSERT_STR_EQ(cfg.model, "gpt-5.4");
    config_free(&cfg);
    return 0;
}

static int test_config_max_tokens(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("fake-key");
    cfg.compat_base_url = nc_xstrdup("https://example.com/v1");
    char *argv[] = {NC_BINARY_NAME, "-t", "16384"};
    int rc = config_parse(&cfg, 3, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.max_tokens, 16384);
    config_free(&cfg);
    return 0;
}

static int test_config_system_prompt(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.compat_api_key = nc_xstrdup("fake-key");
    cfg.compat_base_url = nc_xstrdup("https://example.com/v1");
    char *argv[] = {NC_BINARY_NAME, "-s", "Be concise."};
    int rc = config_parse(&cfg, 3, argv);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(cfg.system_prompt_override, "Be concise.");
    config_free(&cfg);
    return 0;
}

int test_config_extra_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("config_extra");
    RUN_TEST(test_config_json_flag);
    RUN_TEST(test_config_output_flag);
    RUN_TEST(test_config_json_default_false);
    RUN_TEST(test_config_combined_flags);
    RUN_TEST(test_config_max_tokens);
    RUN_TEST(test_config_system_prompt);
    RUN_TEST(test_config_append_system_prompt_file);
    TEST_RETURN_FAILED();
}
