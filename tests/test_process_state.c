#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>
static int test_ps_init(void) {
    process_state_init();
    ASSERT_INT_EQ(process_state_total(), 0);
    process_state_free();
    return 0;
}
static int test_ps_reg(void) {
    process_state_init();
    ASSERT(process_state_register(12345, "p", 5000) >= 0);
    ASSERT_TRUE(process_state_alive(12345));
    process_state_free();
    return 0;
}
static int test_ps_trans(void) {
    process_state_init();
    process_state_register(100, "w", 10000);
    ASSERT_INT_EQ(process_state_set_running(100), 0);
    ASSERT_INT_EQ(process_state_get(100), PS_RUNNING);
    ASSERT_INT_EQ(process_state_yield(100), 0);
    ASSERT_INT_EQ(process_state_resume(100), 0);
    ASSERT_INT_EQ(process_state_set_exited(100, 0), 0);
    ASSERT_FALSE(process_state_alive(100));
    process_state_free();
    return 0;
}
static int test_ps_inv(void) {
    process_state_init();
    process_state_register(200, "b", 5000);
    ASSERT_INT_EQ(process_state_yield(200), -1);
    ASSERT_INT_EQ(process_state_set_running(200), 0);
    ASSERT_INT_EQ(process_state_transition(200, PS_CREATED), -1);
    process_state_free();
    return 0;
}
static int test_ps_kill(void) {
    process_state_init();
    process_state_register(300, "v", 5000);
    process_state_set_running(300);
    ASSERT_INT_EQ(process_state_set_killed(300, 9), 0);
    ASSERT_FALSE(process_state_alive(300));
    process_state_free();
    return 0;
}
static int test_ps_names(void) {
    ASSERT_STR_EQ(pstate_phase_name(PS_CREATED), "created");
    ASSERT_STR_EQ(pstate_phase_name(PS_RUNNING), "running");
    ASSERT_STR_EQ(pstate_phase_name(PS_EXITED), "exited");
    return 0;
}
static int test_ps_fmt(void) {
    process_state_init();
    process_state_register(600, "fm", 5000);
    char *s = process_state_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "fm");
    free(s);
    process_state_free();
    return 0;
}
static int test_ps_nf(void) {
    process_state_init();
    ASSERT_INT_EQ(process_state_set_running(99999), -1);
    ASSERT_FALSE(process_state_alive(99999));
    process_state_free();
    return 0;
}
int test_process_state_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("process_state");
    RUN_TEST(test_ps_init);
    RUN_TEST(test_ps_reg);
    RUN_TEST(test_ps_trans);
    RUN_TEST(test_ps_inv);
    RUN_TEST(test_ps_kill);
    RUN_TEST(test_ps_names);
    RUN_TEST(test_ps_fmt);
    RUN_TEST(test_ps_nf);
    TEST_RETURN_FAILED();
}
