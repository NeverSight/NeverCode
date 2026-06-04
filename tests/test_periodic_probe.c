#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init_defaults(void) {
    periodic_probe_t pp;
    probe_init(&pp, "workers", 30);
    ASSERT_STR_EQ(pp.watch_group, "workers");
    ASSERT_INT_EQ(pp.interval_s, 30);
    ASSERT_INT_EQ(pp.state, PROBE_IDLE);
    ASSERT_INT_EQ(pp.tick_count, 0);
    probe_free(&pp);
    return 0;
}

static int t_init_default_interval(void) {
    periodic_probe_t pp;
    probe_init(&pp, "g", 0);
    ASSERT_INT_EQ(pp.interval_s, PROBE_TICK_DEFAULT_S);
    probe_free(&pp);
    return 0;
}

static int t_record_tick(void) {
    periodic_probe_t pp;
    probe_init(&pp, "g", 10);
    ASSERT_INT_EQ(pp.state, PROBE_IDLE);
    int tc = probe_record_tick(&pp);
    ASSERT_INT_EQ(tc, 1);
    ASSERT_INT_EQ(pp.state, PROBE_WATCHING);
    ASSERT_INT_EQ(pp.tick_count, 1);
    probe_record_tick(&pp);
    ASSERT_INT_EQ(pp.tick_count, 2);
    probe_free(&pp);
    return 0;
}

static int t_should_tick(void) {
    periodic_probe_t pp;
    probe_init(&pp, "g", 1);
    long now = nc_mono_ms();
    ASSERT_FALSE(probe_should_tick(&pp, now));
    pp.next_tick_ms = now - 1;
    ASSERT_TRUE(probe_should_tick(&pp, now));
    probe_finalize(&pp);
    ASSERT_FALSE(probe_should_tick(&pp, now + 999999));
    probe_free(&pp);
    return 0;
}

static int t_add_action(void) {
    periodic_probe_t pp;
    probe_init(&pp, "g", 10);
    ASSERT_INT_EQ(probe_add_action(&pp, PROBE_ACT_CANCEL, "node_0", "stalled"), 0);
    ASSERT_INT_EQ(pp.nactions, 1);
    ASSERT_INT_EQ(pp.actions[0].kind, PROBE_ACT_CANCEL);
    ASSERT_STR_EQ(pp.actions[0].target_id, "node_0");
    ASSERT_STR_EQ(pp.actions[0].reason, "stalled");
    probe_free(&pp);
    return 0;
}

static int t_action_overflow(void) {
    periodic_probe_t pp;
    probe_init(&pp, "g", 10);
    for (int i = 0; i < PROBE_MAX_ACTIONS; i++)
        ASSERT_INT_EQ(probe_add_action(&pp, PROBE_ACT_RESTART, "n", "r"), 0);
    ASSERT_INT_EQ(probe_add_action(&pp, PROBE_ACT_CANCEL, "n", "r"), -1);
    probe_free(&pp);
    return 0;
}

static int t_finalize(void) {
    periodic_probe_t pp;
    probe_init(&pp, "g", 10);
    probe_record_tick(&pp);
    probe_add_action(&pp, PROBE_ACT_CANCEL, "x", "done");
    probe_finalize(&pp);
    ASSERT_INT_EQ(pp.state, PROBE_DONE);
    ASSERT_INT_EQ(pp.nactions, 0);
    probe_free(&pp);
    return 0;
}

static int t_format_status(void) {
    periodic_probe_t pp;
    probe_init(&pp, "fan", 60);
    probe_record_tick(&pp);
    char *s = probe_format_status(&pp);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "fan");
    ASSERT_STR_CONTAINS(s, "watching");
    free(s);
    probe_free(&pp);
    return 0;
}

static int t_snapshot_restore(void) {
    periodic_probe_t pp;
    probe_init(&pp, "grp", 20);
    probe_record_tick(&pp);
    probe_add_action(&pp, PROBE_ACT_RESTART, "n1", "retry");

    probe_snapshot_t snap;
    probe_snapshot(&pp, &snap);
    ASSERT_STR_EQ(snap.watch_group, "grp");
    ASSERT_INT_EQ(snap.interval_s, 20);
    ASSERT_INT_EQ(snap.tick_count, 1);
    ASSERT_INT_EQ(snap.state, PROBE_WATCHING);
    ASSERT_INT_EQ(snap.nactions, 1);

    periodic_probe_t pp2;
    probe_restore(&pp2, &snap);
    ASSERT_STR_EQ(pp2.watch_group, "grp");
    ASSERT_INT_EQ(pp2.tick_count, 1);
    ASSERT_INT_EQ(pp2.state, PROBE_WATCHING);
    ASSERT_INT_EQ(pp2.nactions, 1);

    probe_free(&pp);
    probe_free(&pp2);
    return 0;
}

static int t_snapshot_null(void) {
    probe_snapshot_t snap;
    probe_snapshot(NULL, &snap);
    ASSERT_INT_EQ(snap.tick_count, 0);

    periodic_probe_t pp;
    probe_restore(&pp, NULL);
    ASSERT_INT_EQ(pp.state, PROBE_IDLE);
    probe_free(&pp);
    return 0;
}

int test_periodic_probe_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("periodic_probe");
    RUN_TEST(t_init_defaults);
    RUN_TEST(t_init_default_interval);
    RUN_TEST(t_record_tick);
    RUN_TEST(t_should_tick);
    RUN_TEST(t_add_action);
    RUN_TEST(t_action_overflow);
    RUN_TEST(t_finalize);
    RUN_TEST(t_format_status);
    RUN_TEST(t_snapshot_restore);
    RUN_TEST(t_snapshot_null);
    TEST_RETURN_FAILED();
}
