#include "test.h"
#include "nevercode.h"
static int t1(void) {
    resource_monitor_init();
    ASSERT_INT_EQ(resource_monitor_sample_count(), 0);
    resource_monitor_free();
    return 0;
}
static int t2(void) {
    resource_monitor_init();
    int rc = resource_monitor_poll();
    ASSERT(rc >= 0);
    resource_monitor_free();
    return 0;
}
static int t3(void) {
    resource_monitor_init();
    resource_monitor_poll();
    char *s = resource_monitor_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Resource Monitor");
    free(s);
    resource_monitor_free();
    return 0;
}
static int t4(void) {
    resource_monitor_init();
    ASSERT_FALSE(resource_monitor_low_memory(99));
    ASSERT_FALSE(resource_monitor_low_disk(1));
    resource_monitor_free();
    return 0;
}
int test_resource_monitor_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("resource_monitor");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    TEST_RETURN_FAILED();
}
