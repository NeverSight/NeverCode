#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>
static int test_deleg_init(void) {
    delegate_init();
    ASSERT_INT_EQ(delegate_count(), 0);
    delegate_free();
    return 0;
}
static int test_deleg_names(void) {
    ASSERT_STR_EQ(delegate_phase_name(DELEG_IDLE), "idle");
    ASSERT_STR_EQ(delegate_phase_name(DELEG_DONE), "done");
    return 0;
}
static int test_deleg_format(void) {
    delegate_init();
    char *s = delegate_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "0 total");
    free(s);
    delegate_free();
    return 0;
}
static int test_deleg_cancel_bad(void) {
    delegate_init();
    ASSERT_INT_EQ(delegate_cancel("nope"), -1);
    delegate_free();
    return 0;
}
static int test_deleg_send_bad(void) {
    delegate_init();
    ASSERT_INT_EQ(delegate_send_input("nope", "hi"), -1);
    delegate_free();
    return 0;
}
static int test_deleg_wait_bad(void) {
    delegate_init();
    char *r = NULL;
    ASSERT_INT_EQ(delegate_wait("nope", 100, &r), -1);
    ASSERT_NULL(r);
    delegate_free();
    return 0;
}
static int test_deleg_phase_bad(void) {
    delegate_init();
    ASSERT_INT_EQ(delegate_phase("nope"), DELEG_IDLE);
    delegate_free();
    return 0;
}
int test_delegate_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("delegate");
    RUN_TEST(test_deleg_init);
    RUN_TEST(test_deleg_names);
    RUN_TEST(test_deleg_format);
    RUN_TEST(test_deleg_cancel_bad);
    RUN_TEST(test_deleg_send_bad);
    RUN_TEST(test_deleg_wait_bad);
    RUN_TEST(test_deleg_phase_bad);
    TEST_RETURN_FAILED();
}
