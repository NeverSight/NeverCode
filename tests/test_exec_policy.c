#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>
static int test_ep_init(void) {
    exec_policy_init();
    ASSERT_INT_EQ(exec_policy_mode(), EP_FULL_ACCESS);
    exec_policy_free();
    return 0;
}
static int test_ep_full(void) {
    exec_policy_init();
    ASSERT_TRUE(exec_policy_check_path("/etc/passwd", true));
    ASSERT_TRUE(exec_policy_check_network("evil.com"));
    exec_policy_free();
    return 0;
}
static int test_ep_readonly(void) {
    exec_policy_init();
    exec_policy_set_mode(EP_READ_ONLY);
    ASSERT_TRUE(exec_policy_check_path("/tmp/foo", false));
    ASSERT_FALSE(exec_policy_check_path("/tmp/foo", true));
    exec_policy_free();
    return 0;
}
static int test_ep_paths(void) {
    exec_policy_init();
    exec_policy_set_mode(EP_CUSTOM);
    exec_policy_grant_path("/home/*", true);
    ASSERT_TRUE(exec_policy_check_path("/home/user/f.txt", true));
    ASSERT_INT_EQ(exec_policy_path_grant_count(), 1);
    exec_policy_free();
    return 0;
}
static int test_ep_nets(void) {
    exec_policy_init();
    exec_policy_set_mode(EP_CUSTOM);
    exec_policy_grant_network("evil.com", false);
    ASSERT_FALSE(exec_policy_check_network("evil.com"));
    exec_policy_free();
    return 0;
}
static int test_ep_names(void) {
    ASSERT_STR_EQ(exec_policy_mode_name(EP_FULL_ACCESS), "full_access");
    ASSERT_STR_EQ(exec_policy_mode_name(EP_READ_ONLY), "read_only");
    return 0;
}
static int test_ep_ser(void) {
    exec_policy_init();
    exec_policy_set_mode(EP_CUSTOM);
    exec_policy_grant_path("/home/*", true);
    char *json = exec_policy_serialize();
    ASSERT_NOT_NULL(json);
    exec_policy_free();
    exec_policy_init();
    ASSERT_INT_EQ(exec_policy_deserialize(json), 0);
    ASSERT_INT_EQ(exec_policy_mode(), EP_CUSTOM);
    free(json);
    exec_policy_free();
    return 0;
}
static int test_ep_fmt(void) {
    exec_policy_init();
    char *s = exec_policy_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "full_access");
    free(s);
    exec_policy_free();
    return 0;
}
int test_exec_policy_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("exec_policy");
    RUN_TEST(test_ep_init);
    RUN_TEST(test_ep_full);
    RUN_TEST(test_ep_readonly);
    RUN_TEST(test_ep_paths);
    RUN_TEST(test_ep_nets);
    RUN_TEST(test_ep_names);
    RUN_TEST(test_ep_ser);
    RUN_TEST(test_ep_fmt);
    TEST_RETURN_FAILED();
}
