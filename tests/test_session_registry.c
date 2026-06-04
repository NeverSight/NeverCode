#include "test.h"
#include "nevercode.h"

static int t_not_registered_initially(void) {
    ASSERT_FALSE(sreg_is_registered());
    return 0;
}

static int t_register_unregister(void) {
    int rc = sreg_register("test-session-001");
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(sreg_is_registered());
    sreg_unregister();
    ASSERT_FALSE(sreg_is_registered());
    return 0;
}

static int t_count_live(void) {
    int n = sreg_count_live();
    ASSERT_GE(n, 0);
    return 0;
}

static int t_list_live(void) {
    char *buf = NULL;
    int n = sreg_list_live(&buf);
    ASSERT_NOT_NULL(buf);
    ASSERT_GE(n, 0);
    free(buf);
    return 0;
}

static int t_null_list(void) {
    ASSERT_INT_EQ(sreg_list_live(NULL), 0);
    return 0;
}

static int t_macros(void) {
    ASSERT_GT(SREG_MAX_SESSIONS, 0);
    return 0;
}

int test_session_registry_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_registry");
    RUN_TEST(t_not_registered_initially);
    RUN_TEST(t_register_unregister);
    RUN_TEST(t_count_live);
    RUN_TEST(t_list_live);
    RUN_TEST(t_null_list);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
