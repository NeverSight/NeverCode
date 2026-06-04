#include "test.h"
#include "nevercode.h"

static int t_init(void) {
    pinned_msg_init();
    ASSERT_INT_EQ(pinned_msg_count(), 0);
    return 0;
}

static int t_pin_unpin(void) {
    pinned_msg_init();
    ASSERT_TRUE(pinned_msg_pin(3));
    ASSERT_TRUE(pinned_msg_is_pinned(3));
    ASSERT_INT_EQ(pinned_msg_count(), 1);

    ASSERT_TRUE(pinned_msg_unpin(3));
    ASSERT_FALSE(pinned_msg_is_pinned(3));
    ASSERT_INT_EQ(pinned_msg_count(), 0);
    return 0;
}

static int t_toggle(void) {
    pinned_msg_init();
    ASSERT_TRUE(pinned_msg_toggle(5));
    ASSERT_TRUE(pinned_msg_is_pinned(5));
    ASSERT_FALSE(pinned_msg_toggle(5));
    ASSERT_FALSE(pinned_msg_is_pinned(5));
    return 0;
}

static int t_lowest(void) {
    pinned_msg_init();
    pinned_msg_pin(10);
    pinned_msg_pin(3);
    pinned_msg_pin(7);
    ASSERT_INT_EQ(pinned_msg_lowest_pinned(), 3);
    return 0;
}

static int t_shift(void) {
    pinned_msg_init();
    pinned_msg_pin(2);
    pinned_msg_pin(5);
    pinned_msg_pin(8);

    pinned_msg_shift(3, 2);

    ASSERT_TRUE(pinned_msg_is_pinned(2));
    ASSERT_TRUE(pinned_msg_is_pinned(3));
    ASSERT_TRUE(pinned_msg_is_pinned(6));
    ASSERT_INT_EQ(pinned_msg_count(), 3);
    return 0;
}

static int t_format(void) {
    pinned_msg_init();
    pinned_msg_pin(1);
    pinned_msg_pin(4);
    char *s = pinned_msg_format(10);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "2");
    free(s);
    return 0;
}

static int t_dedup(void) {
    pinned_msg_init();
    ASSERT_TRUE(pinned_msg_pin(3));
    ASSERT_TRUE(pinned_msg_pin(3));
    ASSERT_INT_EQ(pinned_msg_count(), 1);
    return 0;
}

static int t_clear(void) {
    pinned_msg_init();
    pinned_msg_pin(1);
    pinned_msg_pin(2);
    pinned_msg_clear();
    ASSERT_INT_EQ(pinned_msg_count(), 0);
    return 0;
}

int test_pinned_msg_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("pinned_msg");
    RUN_TEST(t_init);
    RUN_TEST(t_pin_unpin);
    RUN_TEST(t_toggle);
    RUN_TEST(t_lowest);
    RUN_TEST(t_shift);
    RUN_TEST(t_format);
    RUN_TEST(t_dedup);
    RUN_TEST(t_clear);
    TEST_RETURN_FAILED();
}
