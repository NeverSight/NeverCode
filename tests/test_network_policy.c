#include "test.h"
#include "nevercode.h"

static int test_init(void) {
    network_policy_t p;
    network_policy_init(&p);
    ASSERT_INT_EQ(p.num_rules, 0);
    ASSERT_INT_EQ(p.default_action, NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_add_rule(void) {
    network_policy_t p;
    network_policy_init(&p);
    ASSERT_INT_EQ(network_policy_add_rule(&p, "*.example.com", NET_ACTION_ALLOW), 0);
    ASSERT_INT_EQ(p.num_rules, 1);
    network_policy_free(&p);
    return 0;
}

static int test_check_allow(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "api.example.com", NET_ACTION_ALLOW);
    ASSERT_INT_EQ(network_policy_check(&p, "api.example.com"), NET_ACTION_ALLOW);
    ASSERT_INT_EQ(network_policy_check(&p, "other.com"), NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_check_deny(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "evil.com", NET_ACTION_DENY);
    ASSERT_INT_EQ(network_policy_check(&p, "evil.com"), NET_ACTION_DENY);
    ASSERT_INT_EQ(network_policy_check(&p, "good.com"), NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_check_wildcard(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "*.github.com", NET_ACTION_ALLOW);
    ASSERT_INT_EQ(network_policy_check(&p, "api.github.com"), NET_ACTION_ALLOW);
    ASSERT_INT_EQ(network_policy_check(&p, "raw.github.com"), NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_check_url(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "example.com", NET_ACTION_ALLOW);
    ASSERT_INT_EQ(network_policy_check_url(&p, "https://example.com/api/v1"), NET_ACTION_ALLOW);
    ASSERT_INT_EQ(network_policy_check_url(&p, "http://other.com/path"), NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_extract_urls(void) {
    char *urls = network_policy_extract_urls_from_cmd("curl https://api.example.com/data");
    ASSERT_NOT_NULL(urls);
    ASSERT(strstr(urls, "api.example.com") != NULL);
    free(urls);
    return 0;
}

static int test_extract_no_urls(void) {
    char *urls = network_policy_extract_urls_from_cmd("ls -la");
    ASSERT_NULL(urls);
    return 0;
}

static int test_check_cmd(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "allowed.com", NET_ACTION_ALLOW);
    network_policy_add_rule(&p, "blocked.com", NET_ACTION_DENY);

    ASSERT_INT_EQ(network_policy_check_cmd(&p, "curl https://allowed.com/api"), NET_ACTION_ALLOW);
    ASSERT_INT_EQ(network_policy_check_cmd(&p, "wget https://blocked.com/data"), NET_ACTION_DENY);
    ASSERT_INT_EQ(network_policy_check_cmd(&p, "ls -la"), NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_default_allow(void) {
    network_policy_t p;
    network_policy_init(&p);
    p.default_action = NET_ACTION_ALLOW;
    ASSERT_INT_EQ(network_policy_check(&p, "any.com"), NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_format(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "*.example.com", NET_ACTION_ALLOW);
    network_policy_add_rule(&p, "evil.com", NET_ACTION_DENY);
    char *fmt = network_policy_format(&p);
    ASSERT_NOT_NULL(fmt);
    ASSERT(strstr(fmt, "Network Policy") != NULL);
    ASSERT(strstr(fmt, "allow") != NULL);
    ASSERT(strstr(fmt, "deny") != NULL);
    free(fmt);
    network_policy_free(&p);
    return 0;
}

static int test_null_args(void) {
    ASSERT_INT_EQ(network_policy_check(NULL, "host"), NET_ACTION_ASK);
    ASSERT_INT_EQ(network_policy_check_url(NULL, "http://x"), NET_ACTION_ASK);
    ASSERT_NULL(network_policy_extract_urls_from_cmd(NULL));
    ASSERT_INT_EQ(network_policy_add_rule(NULL, "x", NET_ACTION_ALLOW), -1);
    return 0;
}

int test_network_policy_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("network_policy");
    RUN_TEST(test_init);
    RUN_TEST(test_add_rule);
    RUN_TEST(test_check_allow);
    RUN_TEST(test_check_deny);
    RUN_TEST(test_check_wildcard);
    RUN_TEST(test_check_url);
    RUN_TEST(test_extract_urls);
    RUN_TEST(test_extract_no_urls);
    RUN_TEST(test_check_cmd);
    RUN_TEST(test_default_allow);
    RUN_TEST(test_format);
    RUN_TEST(test_null_args);
    TEST_RETURN_FAILED();
}
