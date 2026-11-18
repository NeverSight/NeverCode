#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init(void) {
    ledger_watcher_t w;
    ledger_watcher_init(&w, 42);
    ASSERT_INT_EQ(w.watcher_id, 42);
    ASSERT_TRUE(w.active);
    ASSERT_INT_EQ(w.count, 0);
    return 0;
}

static int t_subscribe_unsubscribe(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_watcher_t w;
    ledger_watcher_init(&w, 1);

    ASSERT_INT_EQ(ledger_subscribe(&el, &w), 0);
    ASSERT_INT_EQ(el.nwatchers, 1);
    ASSERT_INT_EQ(ledger_subscribe(&el, &w), 0);
    ASSERT_INT_EQ(el.nwatchers, 1);

    ledger_unsubscribe(&el, &w);
    ASSERT_INT_EQ(el.nwatchers, 0);
    ASSERT_FALSE(w.active);

    ledger_free(&el);
    return 0;
}

static int t_null_args(void) {
    ASSERT_INT_EQ(ledger_subscribe(NULL, NULL), -1);
    ledger_unsubscribe(NULL, NULL);
    ASSERT_INT_EQ(ledger_notify_watchers(NULL, NULL), -1);
    return 0;
}

static int t_notify_and_poll(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_watcher_t w;
    ledger_watcher_init(&w, 1);
    ledger_subscribe(&el, &w);

    ledger_event_t ev;
    memset(&ev, 0, sizeof(ev));
    nc_strlcpy(ev.type, "node_done", sizeof(ev.type));
    ev.sequence = 1;

    int notified = ledger_notify_watchers(&el, &ev);
    ASSERT_INT_EQ(notified, 1);
    ASSERT_TRUE(ledger_watcher_has_events(&w));
    ASSERT_INT_EQ(ledger_watcher_pending(&w), 1);

    ledger_event_t out[4];
    int n = ledger_watcher_poll(&w, out, 4);
    ASSERT_INT_EQ(n, 1);
    ASSERT_STR_EQ(out[0].type, "node_done");
    ASSERT_FALSE(ledger_watcher_has_events(&w));

    ledger_free(&el);
    return 0;
}

static int t_overflow_drops_oldest(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_watcher_t w;
    ledger_watcher_init(&w, 1);
    ledger_subscribe(&el, &w);

    for (int i = 0; i < LEDGER_WATCHER_QUEUE + 5; i++) {
        ledger_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.sequence = i + 1;
        snprintf(ev.type, sizeof(ev.type), "ev_%d", i);
        ledger_notify_watchers(&el, &ev);
    }

    ASSERT_INT_EQ(w.count, LEDGER_WATCHER_QUEUE);
    ledger_event_t out[1];
    int n = ledger_watcher_poll(&w, out, 1);
    ASSERT_INT_EQ(n, 1);
    ASSERT_GT(out[0].sequence, 5);

    ledger_free(&el);
    return 0;
}

static int t_inactive_watcher_ignored(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_watcher_t w;
    ledger_watcher_init(&w, 1);
    ledger_subscribe(&el, &w);
    w.active = false;

    ledger_event_t ev;
    memset(&ev, 0, sizeof(ev));
    nc_strlcpy(ev.type, "test", sizeof(ev.type));
    int notified = ledger_notify_watchers(&el, &ev);
    ASSERT_INT_EQ(notified, 0);
    ASSERT_INT_EQ(ledger_watcher_pending(&w), 0);

    ledger_free(&el);
    return 0;
}

static int t_subscribe_overflow(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_watcher_t watchers[LEDGER_MAX_WATCHERS + 1];
    for (int i = 0; i < LEDGER_MAX_WATCHERS; i++) {
        ledger_watcher_init(&watchers[i], i);
        ASSERT_INT_EQ(ledger_subscribe(&el, &watchers[i]), 0);
    }
    ledger_watcher_init(&watchers[LEDGER_MAX_WATCHERS], 99);
    ASSERT_INT_EQ(ledger_subscribe(&el, &watchers[LEDGER_MAX_WATCHERS]), -1);
    ledger_free(&el);
    return 0;
}

static int t_poll_empty(void) {
    ledger_watcher_t w;
    ledger_watcher_init(&w, 1);
    ledger_event_t out[4];
    ASSERT_INT_EQ(ledger_watcher_poll(&w, out, 4), 0);
    ASSERT_INT_EQ(ledger_watcher_poll(NULL, out, 4), 0);
    ASSERT_INT_EQ(ledger_watcher_poll(&w, out, 0), 0);
    return 0;
}

int test_ledger_watcher_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("ledger_watcher");
    RUN_TEST(t_init);
    RUN_TEST(t_subscribe_unsubscribe);
    RUN_TEST(t_null_args);
    RUN_TEST(t_notify_and_poll);
    RUN_TEST(t_inactive_watcher_ignored);
    RUN_TEST(t_subscribe_overflow);
    RUN_TEST(t_overflow_drops_oldest);
    RUN_TEST(t_poll_empty);
    TEST_RETURN_FAILED();
}
