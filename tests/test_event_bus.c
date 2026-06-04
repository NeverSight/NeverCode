/*
 * test_event_bus.c
 */

#include "test.h"
#include "nevercode.h"
#include <string.h>

static int hits;
static event_type_t got;

static void hdl(const event_t *e, void *ctx) {
    (void)ctx;
    hits++;
    if (e)
        got = e->type;
}

static int test_emit_deliver(void) {
    event_bus_init();
    hits = 0;
    int sid = event_bus_subscribe(EVT_TURN_START, hdl, NULL);
    ASSERT(sid >= 0);
    ASSERT_INT_EQ(event_bus_emit_simple(EVT_TURN_START, "go"), 1);
    ASSERT_INT_EQ(hits, 1);
    ASSERT_INT_EQ((int)got, (int)EVT_TURN_START);
    ASSERT_INT_EQ(event_bus_unsubscribe(sid), 0);
    event_bus_free();
    return 0;
}

static int test_tool_event(void) {
    event_bus_init();
    hits = 0;
    event_bus_subscribe(EVT_TOOL_CALL_START, hdl, NULL);
    ASSERT_INT_EQ(event_bus_emit_tool(EVT_TOOL_CALL_START, "bash", "x", 3), 1);
    ASSERT_INT_EQ(hits, 1);
    event_bus_free();
    return 0;
}

static int test_stats(void) {
    event_bus_init();
    event_bus_subscribe(EVT_CONFIG_RELOAD, hdl, NULL);
    event_bus_emit_simple(EVT_CONFIG_RELOAD, "r");
    char *f = event_bus_format_stats();
    ASSERT_NOT_NULL(f);
    ASSERT(strstr(f, "EventBus") != NULL);
    free(f);
    event_bus_free();
    return 0;
}

int test_event_bus_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("event_bus");
    RUN_TEST(test_emit_deliver);
    RUN_TEST(test_tool_event);
    RUN_TEST(test_stats);
    TEST_RETURN_FAILED();
}
