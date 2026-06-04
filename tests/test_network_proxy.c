#include "test.h"
#include "nevercode.h"

int nproxy_load_env(void);
int nproxy_set(int type, const char *url);
int nproxy_set_no_proxy(const char *hosts);
const char *nproxy_get_url(int type);
int nproxy_is_active(void);
const char *nproxy_resolve_for_host(const char *host);
char *nproxy_format_status(void);

static int test_load_env_empty(void) {
    unsetenv("HTTP_PROXY");
    unsetenv("http_proxy");
    unsetenv("HTTPS_PROXY");
    unsetenv("https_proxy");
    unsetenv("ALL_PROXY");
    unsetenv("all_proxy");
    unsetenv("NO_PROXY");
    unsetenv("no_proxy");

    int r = nproxy_load_env();
    ASSERT_INT_EQ(r, 0);
    ASSERT_FALSE(nproxy_is_active());
    return 0;
}

static int test_set_http_proxy(void) {
    nproxy_load_env();
    nproxy_set(1 /* NPROXY_HTTP */, "http://proxy.example:8080");
    ASSERT_TRUE(nproxy_is_active());
    ASSERT_STR_EQ(nproxy_get_url(1), "http://proxy.example:8080");
    return 0;
}

static int test_set_no_proxy(void) {
    nproxy_load_env();
    nproxy_set(2 /* NPROXY_HTTPS */, "https://proxy.example:443");
    nproxy_set_no_proxy("localhost,127.0.0.1,.internal.com");

    ASSERT_NULL(nproxy_resolve_for_host("localhost"));
    ASSERT_NULL(nproxy_resolve_for_host("127.0.0.1"));
    ASSERT_NULL(nproxy_resolve_for_host("app.internal.com"));
    ASSERT_NOT_NULL(nproxy_resolve_for_host("api.openai.com"));
    return 0;
}

static int test_format_status_inactive(void) {
    nproxy_load_env();
    char *s = nproxy_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "No proxy");
    free(s);
    return 0;
}

static int test_format_status_active(void) {
    nproxy_load_env();
    nproxy_set(1, "http://myproxy:3128");
    char *s = nproxy_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "HTTP proxy");
    ASSERT_STR_CONTAINS(s, "myproxy");
    free(s);
    return 0;
}

int test_network_proxy_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("network_proxy");
    RUN_TEST(test_load_env_empty);
    RUN_TEST(test_set_http_proxy);
    RUN_TEST(test_set_no_proxy);
    RUN_TEST(test_format_status_inactive);
    RUN_TEST(test_format_status_active);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("network_proxy");
    RUN_TEST(test_load_env_empty);
    RUN_TEST(test_set_http_proxy);
    RUN_TEST(test_set_no_proxy);
    RUN_TEST(test_format_status_inactive);
    RUN_TEST(test_format_status_active);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
