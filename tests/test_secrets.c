#include "test.h"
#include "nevercode.h"

static int t_no_secrets(void) {
    secrets_init();
    ASSERT_FALSE(secrets_contains("hello world, no secrets here"));
    secrets_free();
    return 0;
}

static int t_detect_sk_key(void) {
    secrets_init();
    ASSERT_TRUE(secrets_contains("key=sk-abcdefghijklmnopqrstuvwx"));
    secrets_free();
    return 0;
}

static int t_detect_ghp(void) {
    secrets_init();
    ASSERT_TRUE(secrets_contains("token ghp_aBcDeFgHiJkLmNoPqRsTuVwXyZ0123456789"));
    secrets_free();
    return 0;
}

static int t_detect_aws(void) {
    secrets_init();
    ASSERT_TRUE(secrets_contains("AKIA1234567890ABCDEF"));
    secrets_free();
    return 0;
}

static int t_pattern_count(void) {
    ASSERT_GE(secrets_count_patterns(), 20);
    return 0;
}

static int t_null_input(void) {
    secrets_init();
    ASSERT_FALSE(secrets_contains(NULL));
    ASSERT_FALSE(secrets_contains(""));
    secrets_free();
    return 0;
}

static int t_redact_sk_key(void) {
    secrets_init();
    char *result = secrets_redact("my key is sk-abcdefghijklmnopqrstuvwx done");
    ASSERT_TRUE(result != NULL);
    ASSERT_TRUE(strstr(result, "sk-abc") != NULL);
    ASSERT_TRUE(strstr(result, "...") != NULL);
    ASSERT_TRUE(strstr(result, "done") != NULL);
    ASSERT_TRUE(strstr(result, "abcdefghijklmnopqrstuvwx") == NULL);
    free(result);
    secrets_free();
    return 0;
}

static int t_redact_no_secrets(void) {
    secrets_init();
    char *result = secrets_redact("nothing to redact here");
    ASSERT_TRUE(result != NULL);
    ASSERT_STR_EQ(result, "nothing to redact here");
    free(result);
    secrets_free();
    return 0;
}

static int t_redact_null(void) {
    secrets_init();
    char *result = secrets_redact(NULL);
    ASSERT_TRUE(result != NULL);
    ASSERT_STR_EQ(result, "");
    free(result);
    secrets_free();
    return 0;
}

static int t_allow_list(void) {
    secrets_init();
    secrets_allow("sk-abcdefghijklmnopqrstuvwx");
    ASSERT_FALSE(secrets_contains("sk-abcdefghijklmnopqrstuvwx"));
    secrets_free();
    return 0;
}

static int t_detect_new_prefixes(void) {
    secrets_init();
    ASSERT_TRUE(secrets_contains("key=fal_aBcDeFgHiJkLmNo"));
    ASSERT_TRUE(secrets_contains("bb_live_aBcDeFgHiJ"));
    ASSERT_TRUE(secrets_contains("tvly-aBcDeFgHiJkLmNo"));
    ASSERT_TRUE(secrets_contains("exa_aBcDeFgHiJkLmNo"));
    secrets_free();
    return 0;
}

static int t_redact_env_assign(void) {
    secrets_init();
    char *result = secrets_redact("export MY_API_KEY=sk_live_abcdefghijklmnopqrst");
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(strstr(result, "abcdefghijklmnopqrst") == NULL);
    free(result);
    secrets_free();
    return 0;
}

static int t_redact_json_field(void) {
    secrets_init();
    char *result = secrets_redact("{\"api_key\": \"super_secret_token_value_12345\"}");
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(strstr(result, "super_secret_token_value_12345") == NULL);
    ASSERT_STR_CONTAINS(result, "...");
    free(result);
    secrets_free();
    return 0;
}

static int t_pattern_count_expanded(void) {
    ASSERT_GE(secrets_count_patterns(), 30);
    return 0;
}

int test_secrets_all(void) {
    TEST_SUITE("secrets");
    TEST_COUNTERS;
    RUN_TEST(t_no_secrets);
    RUN_TEST(t_detect_sk_key);
    RUN_TEST(t_detect_ghp);
    RUN_TEST(t_detect_aws);
    RUN_TEST(t_pattern_count);
    RUN_TEST(t_null_input);
    RUN_TEST(t_redact_sk_key);
    RUN_TEST(t_redact_no_secrets);
    RUN_TEST(t_redact_null);
    RUN_TEST(t_allow_list);
    RUN_TEST(t_detect_new_prefixes);
    RUN_TEST(t_redact_env_assign);
    RUN_TEST(t_redact_json_field);
    RUN_TEST(t_pattern_count_expanded);
    TEST_RETURN_FAILED();
}
