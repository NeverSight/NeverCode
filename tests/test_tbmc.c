#include "test.h"
#include "nevercode.h"
static int t1(void) {
    context_compact_touch_assistant();
    ASSERT(context_compact_last_assistant() > 0);
    return 0;
}
static int t2(void) {
    ASSERT_INT_EQ(context_compact_pre_call(NULL, 60, 5), 0);
    return 0;
}
static int t3(void) {
    context_compact_touch_assistant();
    ASSERT_INT_EQ(context_compact_pre_call(NULL, 0, 5), 0);
    return 0;
}
static int t4(void) {
    context_compact_touch_assistant();
    time_t ts = context_compact_last_assistant();
    ASSERT(ts > 0);
    long now = nc_wall_sec();
    ASSERT(now - (long)ts < 2);
    return 0;
}
int test_tbmc_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("time_based_microcompact");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    TEST_RETURN_FAILED();
}
