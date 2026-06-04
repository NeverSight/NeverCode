#include "test.h"
#include "nevercode.h"
#include "harness.h"
#include <string.h>

static int test_pl_init(void) {
    proc_lifecycle_init();
    ASSERT_INT_EQ(proc_lifecycle_active_count(), 0);
    ASSERT_INT_EQ(proc_lifecycle_total_count(), 0);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_register(void) {
    proc_lifecycle_init();
    int i = proc_lifecycle_register(12345, "process_a");
    ASSERT_TRUE(i >= 0);
    ASSERT_INT_EQ(proc_lifecycle_total_count(), 1);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_running(void) {
    proc_lifecycle_init();
    proc_lifecycle_register(123, "runner");
    proc_lifecycle_set_running(123);
    ASSERT_INT_EQ(proc_lifecycle_active_count(), 1);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_exited(void) {
    proc_lifecycle_init();
    proc_lifecycle_register(123, "exiter");
    proc_lifecycle_set_running(123);
    proc_lifecycle_set_exited(123, 0);
    ASSERT_INT_EQ(proc_lifecycle_active_count(), 0);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_killed(void) {
    proc_lifecycle_init();
    proc_lifecycle_register(123, "killed");
    proc_lifecycle_set_running(123);
    proc_lifecycle_set_killed(123);
    ASSERT_INT_EQ(proc_lifecycle_active_count(), 0);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_format(void) {
    proc_lifecycle_init();
    proc_lifecycle_register(100, "alpha");
    proc_lifecycle_register(200, "beta");
    char *s = proc_lifecycle_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "alpha");
    ASSERT_STR_CONTAINS(s, "beta");
    free(s);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_many(void) {
    proc_lifecycle_init();
    for (int i = 0; i < 10; i++) {
        char label[32];
        snprintf(label, sizeof(label), "proc_%d", i);
        proc_lifecycle_register(1000 + i, label);
    }
    ASSERT_INT_EQ(proc_lifecycle_total_count(), 10);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_output_tracking(void) {
    proc_lifecycle_init();
    proc_lifecycle_register(500, "output_test");
    proc_lifecycle_record_output(500, 1024);
    proc_lifecycle_record_output(500, 2048);
    char *s = proc_lifecycle_format();
    ASSERT_STR_CONTAINS(s, "3072B");
    free(s);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_double_exit(void) {
    proc_lifecycle_init();
    proc_lifecycle_register(600, "dbl_exit");
    proc_lifecycle_set_running(600);
    proc_lifecycle_set_exited(600, 0);
    proc_lifecycle_set_exited(600, 1);
    ASSERT_INT_EQ(proc_lifecycle_active_count(), 0);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_reap(void) {
    proc_lifecycle_init();
    proc_lifecycle_register(700, "reap_target");
    proc_lifecycle_set_exited(700, 0);
    int reaped = proc_lifecycle_reap_exited();
    ASSERT_TRUE(reaped >= 0);
    proc_lifecycle_free();
    return 0;
}

static int test_pl_unknown_pid(void) {
    proc_lifecycle_init();
    proc_lifecycle_set_running(99999);
    proc_lifecycle_set_exited(99999, 0);
    proc_lifecycle_set_killed(99999);
    proc_lifecycle_record_output(99999, 100);
    ASSERT_INT_EQ(proc_lifecycle_total_count(), 0);
    proc_lifecycle_free();
    return 0;
}

int test_process_lifecycle_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("process_lifecycle");

    RUN_TEST(test_pl_init);
    RUN_TEST(test_pl_register);
    RUN_TEST(test_pl_running);
    RUN_TEST(test_pl_exited);
    RUN_TEST(test_pl_killed);
    RUN_TEST(test_pl_format);
    RUN_TEST(test_pl_many);
    RUN_TEST(test_pl_output_tracking);
    RUN_TEST(test_pl_double_exit);
    RUN_TEST(test_pl_reap);
    RUN_TEST(test_pl_unknown_pid);

    TEST_RETURN_FAILED();
}
