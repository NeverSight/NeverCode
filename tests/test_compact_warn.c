#include "test.h"
#include "nevercode.h"
#include "defaults.h"

static int t1(void) {
    compact_warn_clear();
    ASSERT_FALSE(compact_warn_is_suppressed());
    return 0;
}
static int t2(void) {
    compact_warn_clear();
    compact_warn_suppress();
    ASSERT_TRUE(compact_warn_is_suppressed());
    return 0;
}
static int t3(void) {
    compact_warn_clear();
    compact_warn_suppress();
    for (int i = 0; i < COMPACT_WARN_SUPPRESS_TURNS; i++)
        ASSERT_TRUE(compact_warn_is_suppressed());
    ASSERT_FALSE(compact_warn_is_suppressed());
    return 0;
}
static int t4(void) {
    compact_warn_suppress();
    compact_warn_clear();
    ASSERT_FALSE(compact_warn_is_suppressed());
    return 0;
}

int test_compact_warn_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("compact_warn");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    TEST_RETURN_FAILED();
}
