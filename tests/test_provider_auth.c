#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

/* Declarations from provider_auth.c */
extern void provider_auth_init(void);
extern void provider_auth_free(void);
extern int provider_auth_set_key(const char *provider_id, const char *key);
extern int provider_auth_set_bearer(const char *provider_id, const char *token, long expires_at);
extern int provider_auth_set_refresh(const char *provider_id, const char *refresh_url,
                                     const char *refresh_token);
extern bool provider_auth_needs_refresh(const char *provider_id);
extern int provider_auth_refresh(const char *provider_id);
extern const char *provider_auth_credential(const char *provider_id);
extern bool provider_auth_valid(const char *provider_id);
extern const char *provider_auth_header_name(const char *provider_id);
extern char *provider_auth_header_value(const char *provider_id);
extern int provider_auth_count(void);
extern int provider_auth_total_refreshes(void);
extern char *provider_auth_format(void);

static int test_init_clean(void) {
    provider_auth_init();
    ASSERT_INT_EQ(0, provider_auth_count());
    ASSERT_INT_EQ(0, provider_auth_total_refreshes());
    provider_auth_free();
    return 0;
}

static int test_set_api_key(void) {
    provider_auth_init();
    int rc = provider_auth_set_key("openai", "sk-test-key-12345");
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, provider_auth_count());
    ASSERT_STR_EQ("sk-test-key-12345", provider_auth_credential("openai"));
    ASSERT_TRUE(provider_auth_valid("openai"));
    provider_auth_free();
    return 0;
}

static int test_set_bearer(void) {
    provider_auth_init();
    long future = nc_wall_sec() + 3600;
    int rc = provider_auth_set_bearer("custom", "tok-abc", future);
    ASSERT_INT_EQ(0, rc);
    ASSERT_TRUE(provider_auth_valid("custom"));
    ASSERT_FALSE(provider_auth_needs_refresh("custom"));

    char *hdr = provider_auth_header_value("custom");
    ASSERT_NOT_NULL(hdr);
    ASSERT_STR_CONTAINS(hdr, "Bearer");
    ASSERT_STR_CONTAINS(hdr, "tok-abc");
    free(hdr);
    provider_auth_free();
    return 0;
}

static int test_invalid_provider(void) {
    provider_auth_init();
    ASSERT_NULL(provider_auth_credential("nonexistent"));
    ASSERT_FALSE(provider_auth_valid("nonexistent"));
    ASSERT_FALSE(provider_auth_needs_refresh("nonexistent"));
    provider_auth_free();
    return 0;
}

static int test_overwrite_key(void) {
    provider_auth_init();
    provider_auth_set_key("prov", "key-1");
    provider_auth_set_key("prov", "key-2");
    ASSERT_INT_EQ(1, provider_auth_count());
    ASSERT_STR_EQ("key-2", provider_auth_credential("prov"));
    provider_auth_free();
    return 0;
}

static int test_format(void) {
    provider_auth_init();
    provider_auth_set_key("test-prov", "sk-xyz");
    char *fmt = provider_auth_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "test-prov");
    ASSERT_STR_CONTAINS(fmt, "valid=yes");
    free(fmt);
    provider_auth_free();
    return 0;
}

static int test_null_args(void) {
    provider_auth_init();
    ASSERT_INT_EQ(-1, provider_auth_set_key(NULL, "key"));
    ASSERT_INT_EQ(-1, provider_auth_set_key("id", NULL));
    provider_auth_free();
    return 0;
}

int test_provider_auth_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("provider_auth");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_set_api_key);
    RUN_TEST(test_set_bearer);
    RUN_TEST(test_invalid_provider);
    RUN_TEST(test_overwrite_key);
    RUN_TEST(test_format);
    RUN_TEST(test_null_args);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("provider_auth");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_set_api_key);
    RUN_TEST(test_set_bearer);
    RUN_TEST(test_invalid_provider);
    RUN_TEST(test_overwrite_key);
    RUN_TEST(test_format);
    RUN_TEST(test_null_args);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
