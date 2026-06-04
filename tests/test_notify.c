#include "test.h"
#include "terminal.h"

static int test_bel_noop_nontty(void) {
    term_notify_bel();
    return 0;
}

static int test_osc9_null(void) {
    term_notify_osc9(NULL);
    term_notify_osc9("");
    return 0;
}

static int test_osc9_sanitize(void) {
    term_notify_osc9("safe\x1b[31m\x07\x9ctext");
    return 0;
}

static int test_task_done_short(void) {
    term_notify_task_done("build", 3.0);
    return 0;
}

static int test_task_done_long(void) {
    term_notify_task_done("compile", 120.5);
    return 0;
}

static int test_task_done_null(void) {
    term_notify_task_done(NULL, 30.0);
    return 0;
}

static int test_task_done_empty(void) {
    term_notify_task_done("", 15.0);
    return 0;
}

int test_notify_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("notify");
    RUN_TEST(test_bel_noop_nontty);
    RUN_TEST(test_osc9_null);
    RUN_TEST(test_osc9_sanitize);
    RUN_TEST(test_task_done_short);
    RUN_TEST(test_task_done_long);
    RUN_TEST(test_task_done_null);
    RUN_TEST(test_task_done_empty);
    TEST_RETURN_FAILED();
}
