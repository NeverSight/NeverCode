#include "test.h"
#include "nevercode.h"

static int t_init(void) {
    pmon_init();
    char *dash = pmon_format_dashboard();
    ASSERT_NOT_NULL(dash);
    ASSERT_STR_CONTAINS(dash, "0 tracked");
    free(dash);
    return 0;
}

static int t_track(void) {
    pmon_init();
    int idx = pmon_track(99999, "test process");
    ASSERT_TRUE(idx >= 0);
    char *dash = pmon_format_dashboard();
    ASSERT_NOT_NULL(dash);
    ASSERT_STR_CONTAINS(dash, "99999");
    ASSERT_STR_CONTAINS(dash, "test process");
    free(dash);
    return 0;
}

static int t_kill_unknown(void) {
    pmon_init();
    int rc = pmon_kill(12345);
    ASSERT_INT_EQ(rc, -1);
    return 0;
}

static int t_dashboard_format(void) {
    pmon_init();
    pmon_track(1001, "build");
    pmon_track(1002, "test");
    char *dash = pmon_format_dashboard();
    ASSERT_NOT_NULL(dash);
    ASSERT_STR_CONTAINS(dash, "2 tracked");
    ASSERT_STR_CONTAINS(dash, "build");
    ASSERT_STR_CONTAINS(dash, "test");
    free(dash);
    return 0;
}

int test_proc_monitor_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("proc_monitor");
    RUN_TEST(t_init);
    RUN_TEST(t_track);
    RUN_TEST(t_kill_unknown);
    RUN_TEST(t_dashboard_format);
    TEST_RETURN_FAILED();
}
