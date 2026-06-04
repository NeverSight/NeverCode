#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int g_cb_calls = 0;
static void test_handler(const dispatch_event_t *ev, void *ctx) {
    (void)ev;
    (void)ctx;
    g_cb_calls++;
}

static int test_init(void) {
    event_dispatch_init(OUTPUT_QUIET);
    ASSERT_INT_EQ(event_dispatch_mode(), OUTPUT_QUIET);
    ASSERT_INT_EQ(event_dispatch_count(), 0);
    event_dispatch_free();
    return 0;
}
static int test_emit(void) {
    event_dispatch_init(OUTPUT_QUIET);
    event_dispatch_text(1, "hello");
    ASSERT_INT_EQ(event_dispatch_count(), 1);
    event_dispatch_free();
    return 0;
}
static int test_subscribe(void) {
    event_dispatch_init(OUTPUT_QUIET);
    g_cb_calls = 0;
    int mask = (1 << EV_TEXT_DELTA) | (1 << EV_ERROR);
    event_dispatch_subscribe(test_handler, NULL, mask);
    event_dispatch_text(1, "hi");
    ASSERT_INT_EQ(g_cb_calls, 1);
    event_dispatch_tool(1, "bash", false);
    ASSERT_INT_EQ(g_cb_calls, 1);
    event_dispatch_error(1, "oops");
    ASSERT_INT_EQ(g_cb_calls, 2);
    event_dispatch_free();
    return 0;
}
static int test_mode_names(void) {
    ASSERT_STR_EQ(output_mode_name(OUTPUT_HUMAN), "human");
    ASSERT_STR_EQ(output_mode_name(OUTPUT_JSONL), "jsonl");
    ASSERT_STR_EQ(output_mode_name(OUTPUT_QUIET), "quiet");
    return 0;
}
static int test_turn_complete(void) {
    event_dispatch_init(OUTPUT_QUIET);
    event_dispatch_turn_complete(5, 1000, 500, 0.05);
    ASSERT_INT_EQ(event_dispatch_count(), 1);
    event_dispatch_free();
    return 0;
}

static int test_subscribe_overflow(void) {
    event_dispatch_init(OUTPUT_QUIET);
    for (int i = 0; i < EDISP_MAX_SUBSCRIBERS; i++)
        ASSERT_INT_EQ(event_dispatch_subscribe(test_handler, NULL, ~0), 0);
    ASSERT_INT_EQ(event_dispatch_subscribe(test_handler, NULL, ~0), -1);
    event_dispatch_free();
    return 0;
}

static int test_high_kind_safe(void) {
    event_dispatch_init(OUTPUT_QUIET);
    g_cb_calls = 0;
    event_dispatch_subscribe(test_handler, NULL, ~0);
    dispatch_event_t ev = {.kind = (dispatch_event_kind_t)99, .turn = 1, .timestamp = 0};
    event_dispatch_emit(&ev);
    ASSERT_INT_EQ(event_dispatch_count(), 1);
    ASSERT_INT_EQ(g_cb_calls, 0);
    event_dispatch_free();
    return 0;
}

int test_event_dispatch_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("event_dispatch");
    RUN_TEST(test_init);
    RUN_TEST(test_emit);
    RUN_TEST(test_subscribe);
    RUN_TEST(test_mode_names);
    RUN_TEST(test_turn_complete);
    RUN_TEST(test_subscribe_overflow);
    RUN_TEST(test_high_kind_safe);
    TEST_RETURN_FAILED();
}
