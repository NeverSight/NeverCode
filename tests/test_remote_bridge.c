#include "test.h"
#include "nevercode.h"

static int test_init_free(void) {
    remote_bridge_init();
    ASSERT_INT_EQ(remote_bridge_count(), 0);
    remote_bridge_free();
    return 0;
}

static int test_connect_bad_host(void) {
    remote_bridge_init();
    int i = remote_bridge_connect("127.0.0.1", 1, "t");
    ASSERT_TRUE(i == -1 || remote_bridge_count() == 1);
    remote_bridge_free();
    return 0;
}

static int test_format(void) {
    remote_bridge_init();
    char *f = remote_bridge_format_all();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Remote Bridge:");
    free(f);
    remote_bridge_free();
    return 0;
}

static int test_disconnect_invalid(void) {
    remote_bridge_init();
    ASSERT_INT_EQ(remote_bridge_disconnect(-1), -1);
    remote_bridge_free();
    return 0;
}

static int test_double_init(void) {
    remote_bridge_init();
    remote_bridge_free();
    remote_bridge_init();
    ASSERT_INT_EQ(remote_bridge_count(), 0);
    remote_bridge_free();
    return 0;
}

int test_remote_bridge_all(void) {
    TEST_SUITE("remote_bridge");
    TEST_COUNTERS;
    RUN_TEST(test_init_free);
    RUN_TEST(test_connect_bad_host);
    RUN_TEST(test_format);
    RUN_TEST(test_disconnect_invalid);
    RUN_TEST(test_double_init);
    TEST_RETURN_FAILED();
}
