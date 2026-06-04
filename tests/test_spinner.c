#include "test.h"
#include "nevercode.h"
#include "terminal.h"
#include <unistd.h>

static int test_init(void) {
    spinner_t sp;
    spinner_init(&sp);
    ASSERT_FALSE(sp.active);
    ASSERT_INT_EQ(sp.frame, 0);
    return 0;
}

static int test_start(void) {
    spinner_t sp;
    spinner_init(&sp);
    spinner_start(&sp);
    ASSERT_TRUE(sp.active);
    spinner_stop(&sp);
    return 0;
}

static int test_tick_returns_frame(void) {
    spinner_t sp;
    spinner_init(&sp);
    spinner_start(&sp);
    const char *f = spinner_tick(&sp);
    ASSERT_NOT_NULL(f);
    ASSERT_TRUE(strlen(f) > 0);
    spinner_stop(&sp);
    return 0;
}

static int test_tick_inactive(void) {
    spinner_t sp;
    spinner_init(&sp);
    const char *f = spinner_tick(&sp);
    ASSERT(f == NULL);
    return 0;
}

static int test_stop_inactive(void) {
    spinner_t sp;
    spinner_init(&sp);
    spinner_stop(&sp);
    ASSERT_FALSE(sp.active);
    return 0;
}

static int test_null_safety(void) {
    spinner_init(NULL);
    spinner_start(NULL);
    spinner_stop(NULL);
    ASSERT(spinner_tick(NULL) == NULL);
    return 0;
}

static int test_frame_advances(void) {
    spinner_t sp;
    spinner_init(&sp);
    spinner_start(&sp);
    int first = sp.frame;
    usleep((NC_SPINNER_INTERVAL_MS + 10) * 1000);
    spinner_tick(&sp);
    ASSERT_TRUE(sp.frame != first || sp.frame == (NC_SPINNER_FRAME_COUNT - 1));
    spinner_stop(&sp);
    return 0;
}

static int test_render_with_label(void) {
    spinner_t sp;
    spinner_init(&sp);
    spinner_start(&sp);
    spinner_render(&sp, "working", 1.5);
    spinner_stop(&sp);
    return 0;
}

static int test_render_no_label(void) {
    spinner_t sp;
    spinner_init(&sp);
    spinner_start(&sp);
    spinner_render(&sp, NULL, 0);
    spinner_stop(&sp);
    return 0;
}

static int test_render_inactive(void) {
    spinner_t sp;
    spinner_init(&sp);
    spinner_render(&sp, "test", 0);
    return 0;
}

static int test_verb_returns_non_null(void) {
    const char *v = spinner_verb();
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(strlen(v) > 0);
    return 0;
}

static int test_completion_verb_returns_non_null(void) {
    const char *v = spinner_completion_verb();
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(strlen(v) > 0);
    return 0;
}

static int test_verb_stability(void) {
    const char *a = spinner_verb();
    const char *b = spinner_verb();
    ASSERT_STR_EQ(a, b);
    return 0;
}

int test_spinner_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("spinner");
    RUN_TEST(test_init);
    RUN_TEST(test_start);
    RUN_TEST(test_tick_returns_frame);
    RUN_TEST(test_tick_inactive);
    RUN_TEST(test_stop_inactive);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_frame_advances);
    RUN_TEST(test_render_with_label);
    RUN_TEST(test_render_no_label);
    RUN_TEST(test_render_inactive);
    RUN_TEST(test_verb_returns_non_null);
    RUN_TEST(test_completion_verb_returns_non_null);
    RUN_TEST(test_verb_stability);
    TEST_RETURN_FAILED();
}
