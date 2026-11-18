#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init_empty(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ASSERT_INT_EQ(ledger_event_count(&el), 0);
    ASSERT_NULL(ledger_event_at(&el, 0));
    ledger_free(&el);
    return 0;
}

static int t_append_and_count(void) {
    static event_ledger_t el;
    ledger_init(&el);
    int seq1 = ledger_append(&el, "start", "node-a", "began work");
    int seq2 = ledger_append(&el, "done", "node-a", "finished");
    ASSERT_GT(seq1, 0);
    ASSERT_GT(seq2, seq1);
    ASSERT_INT_EQ(ledger_event_count(&el), 2);

    const ledger_event_t *ev = ledger_event_at(&el, 0);
    ASSERT_NOT_NULL(ev);
    ASSERT_STR_EQ(ev->type, "start");
    ASSERT_STR_EQ(ev->node_id, "node-a");
    ledger_free(&el);
    return 0;
}

static int t_append_result(void) {
    static event_ledger_t el;
    ledger_init(&el);
    int seq = ledger_append_result(&el, "completed", "n1", "ok", "output text", 0);
    ASSERT_GT(seq, 0);

    const ledger_event_t *ev = ledger_event_at(&el, 0);
    ASSERT_NOT_NULL(ev);
    ASSERT_TRUE(ev->has_output);
    ASSERT_TRUE(ev->has_exit_code);
    ASSERT_INT_EQ(ev->exit_code, 0);
    ASSERT_STR_CONTAINS(ev->output, "output text");
    ledger_free(&el);
    return 0;
}

static int t_events_since(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_append(&el, "a", NULL, NULL);
    ledger_append(&el, "b", NULL, NULL);
    ledger_append(&el, "c", NULL, NULL);

    const ledger_event_t *out[8];
    int n = ledger_events_since(&el, 1, out, 8);
    ASSERT_INT_EQ(n, 2);
    ASSERT_STR_EQ(out[0]->type, "b");
    ASSERT_STR_EQ(out[1]->type, "c");
    ledger_free(&el);
    return 0;
}

static int t_format_tail(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_append(&el, "alpha", "n1", "detail");
    ledger_append(&el, "beta", "n2", "info");

    char *s = ledger_format_tail(&el, 5);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "alpha");
    ASSERT_STR_CONTAINS(s, "beta");
    ASSERT_STR_CONTAINS(s, "2 events");
    free(s);
    ledger_free(&el);
    return 0;
}

static int t_run_token(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_set_run_token(&el, "run-42");
    ledger_append(&el, "ev", NULL, NULL);

    const ledger_event_t *ev = ledger_event_at(&el, 0);
    ASSERT_NOT_NULL(ev);
    ASSERT_TRUE(ev->has_run_token);
    ASSERT_STR_EQ(ev->run_token, "run-42");
    ledger_free(&el);
    return 0;
}

static int t_out_of_bounds(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ASSERT_NULL(ledger_event_at(&el, -1));
    ASSERT_NULL(ledger_event_at(&el, 0));
    ASSERT_NULL(ledger_event_at(&el, 100));
    ledger_free(&el);
    return 0;
}

int test_event_ledger_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("event_ledger");
    RUN_TEST(t_init_empty);
    RUN_TEST(t_append_and_count);
    RUN_TEST(t_append_result);
    RUN_TEST(t_events_since);
    RUN_TEST(t_format_tail);
    RUN_TEST(t_run_token);
    RUN_TEST(t_out_of_bounds);
    TEST_RETURN_FAILED();
}
