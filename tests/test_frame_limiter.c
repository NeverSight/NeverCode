#include "test.h"
#include "terminal.h"
#include <unistd.h>

static int test_init(void) {
    frame_limiter_t fl;
    frame_limiter_init(&fl);
    ASSERT_INT_EQ(fl.suppressed, 0);
    return 0;
}

static int test_first_draw_allowed(void) {
    frame_limiter_t fl;
    frame_limiter_init(&fl);
    ASSERT_TRUE(frame_limiter_should_draw(&fl));
    return 0;
}

static int test_rapid_draws_suppressed(void) {
    frame_limiter_t fl;
    frame_limiter_init(&fl);
    ASSERT_TRUE(frame_limiter_should_draw(&fl));
    frame_limiter_mark(&fl);
    ASSERT_FALSE(frame_limiter_should_draw(&fl));
    ASSERT_INT_EQ(fl.suppressed, 1);
    return 0;
}

static int test_delayed_draw_allowed(void) {
    frame_limiter_t fl;
    frame_limiter_init(&fl);
    frame_limiter_mark(&fl);
    usleep(20000);
    ASSERT_TRUE(frame_limiter_should_draw(&fl));
    return 0;
}

static int test_null_safety(void) {
    frame_limiter_init(NULL);
    ASSERT_TRUE(frame_limiter_should_draw(NULL));
    frame_limiter_mark(NULL);
    return 0;
}

static int test_suppressed_counter_resets(void) {
    frame_limiter_t fl;
    frame_limiter_init(&fl);
    frame_limiter_mark(&fl);
    frame_limiter_should_draw(&fl);
    frame_limiter_should_draw(&fl);
    ASSERT_INT_EQ(fl.suppressed, 2);
    usleep(20000);
    frame_limiter_should_draw(&fl);
    frame_limiter_mark(&fl);
    ASSERT_INT_EQ(fl.suppressed, 0);
    return 0;
}

int test_frame_limiter_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("frame_limiter");
    RUN_TEST(test_init);
    RUN_TEST(test_first_draw_allowed);
    RUN_TEST(test_rapid_draws_suppressed);
    RUN_TEST(test_delayed_draw_allowed);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_suppressed_counter_resets);
    TEST_RETURN_FAILED();
}
