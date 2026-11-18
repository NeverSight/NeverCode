/*
 * test_pipeline_system.c — Tests for the pipeline DAG, fanout/merge,
 * success criteria, cancel scope, shared pad, event ledger, preflight,
 * periodic probe, retry cycle, and orchestrator modules.
 */

#include "test.h"
#include "test_helpers.h"
#include "defaults.h"
#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ═══════════ Pipeline DAG ═══════════ */

static int t_pipe_basic(void) {
    static pipeline_t p;
    pipeline_init(&p, "test", 4);
    ASSERT_INT_EQ(p.nnodes, 0);
    ASSERT_STR_EQ(p.name, "test");
    ASSERT_INT_EQ(p.concurrency, 4);

    ASSERT_INT_EQ(pipeline_add_node(&p, "a", "do A"), 0);
    ASSERT_INT_EQ(pipeline_add_node(&p, "b", "do B"), 1);
    ASSERT_INT_EQ(pipeline_add_dep(&p, "b", "a"), 0);

    char err[256];
    ASSERT(pipeline_validate(&p, err, sizeof(err)));

    int ready[8];
    int nr = pipeline_ready_nodes(&p, ready, 8);
    ASSERT_INT_EQ(nr, 1);
    ASSERT_INT_EQ(ready[0], 0);

    pipeline_mark_running(&p, 0);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_RUNNING);

    pipeline_mark_done(&p, 0, "result A", 0);
    nr = pipeline_ready_nodes(&p, ready, 8);
    ASSERT_INT_EQ(nr, 1);
    ASSERT_INT_EQ(ready[0], 1);

    pipeline_mark_running(&p, 1);
    pipeline_mark_done(&p, 1, "result B", 0);
    ASSERT(pipeline_all_terminal(&p));

    char *s = pipeline_format_status(&p);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "done") != NULL);
    free(s);

    pipeline_free(&p);
    return 0;
}

static int t_pipe_duplicate_node(void) {
    static pipeline_t p;
    pipeline_init(&p, "dup", 2);
    ASSERT_INT_EQ(pipeline_add_node(&p, "x", "px"), 0);
    ASSERT_INT_EQ(pipeline_add_node(&p, "x", "px2"), -1);
    pipeline_free(&p);
    return 0;
}

static int t_pipe_cycle_detection(void) {
    static pipeline_t p;
    pipeline_init(&p, "cycle", 2);
    pipeline_add_node(&p, "a", "pa");
    pipeline_add_node(&p, "b", "pb");
    pipeline_add_dep(&p, "a", "b");
    pipeline_add_dep(&p, "b", "a");

    char err[256];
    ASSERT(!pipeline_validate(&p, err, sizeof(err)));
    ASSERT(strstr(err, "cycle") != NULL);
    pipeline_free(&p);
    return 0;
}

static int t_pipe_skip_downstream(void) {
    static pipeline_t p;
    pipeline_init(&p, "skip", 4);
    pipeline_add_node(&p, "a", "pa");
    pipeline_add_node(&p, "b", "pb");
    pipeline_add_node(&p, "c", "pc");
    pipeline_add_dep(&p, "b", "a");
    pipeline_add_dep(&p, "c", "b");

    pipeline_mark_running(&p, 0);
    pipeline_mark_failed(&p, 0, "error", 1);

    int skipped = pipeline_skip_downstream(&p, 0);
    ASSERT_INT_EQ(skipped, 2);
    ASSERT_INT_EQ(p.nodes[1].status, PIPE_SKIPPED);
    ASSERT_INT_EQ(p.nodes[2].status, PIPE_SKIPPED);
    ASSERT(pipeline_all_terminal(&p));

    pipeline_free(&p);
    return 0;
}

static int t_pipe_parallel(void) {
    static pipeline_t p;
    pipeline_init(&p, "par", 4);
    pipeline_add_node(&p, "a", "pa");
    pipeline_add_node(&p, "b", "pb");
    pipeline_add_node(&p, "c", "pc");

    int ready[8];
    int nr = pipeline_ready_nodes(&p, ready, 8);
    ASSERT_INT_EQ(nr, 3);

    pipeline_free(&p);
    return 0;
}

static int t_pipe_cancel(void) {
    static pipeline_t p;
    pipeline_init(&p, "cancel", 2);
    pipeline_add_node(&p, "a", "pa");
    pipeline_mark_running(&p, 0);
    pipeline_mark_cancelled(&p, 0);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_CANCELLED);
    ASSERT(pipeline_all_terminal(&p));
    pipeline_free(&p);
    return 0;
}

static int t_pipe_status_names(void) {
    ASSERT_STR_EQ(pipe_status_name(PIPE_PENDING), "pending");
    ASSERT_STR_EQ(pipe_status_name(PIPE_RUNNING), "running");
    ASSERT_STR_EQ(pipe_status_name(PIPE_DONE), "done");
    ASSERT_STR_EQ(pipe_status_name(PIPE_FAILED), "failed");
    ASSERT_STR_EQ(pipe_status_name(PIPE_SKIPPED), "skipped");
    ASSERT_STR_EQ(pipe_status_name(PIPE_CANCELLED), "cancelled");
    return 0;
}

/* ═══════════ Fanout / Merge ═══════════ */

static int t_fanout_count(void) {
    static fanout_spec_t f;
    static pipeline_t p;
    pipeline_init(&p, "fo", 8);

    fanout_init(&f, "worker", "do work");
    fanout_set_count(&f, 4);
    int n = fanout_expand(&f, &p);
    ASSERT_INT_EQ(n, 4);
    ASSERT_INT_EQ(p.nnodes, 4);
    ASSERT_STR_EQ(fanout_expanded_id(&f, 0), "worker_0");
    ASSERT_STR_EQ(fanout_expanded_id(&f, 3), "worker_3");

    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

static int t_fanout_values(void) {
    static fanout_spec_t f;
    static pipeline_t p;
    pipeline_init(&p, "fv", 8);

    fanout_init(&f, "review", "review file");
    fanout_add_value(&f, "api.c");
    fanout_add_value(&f, "auth.c");
    fanout_add_value(&f, "db.c");
    int n = fanout_expand(&f, &p);
    ASSERT_INT_EQ(n, 3);
    ASSERT_INT_EQ(p.nnodes, 3);

    ASSERT(strstr(p.nodes[0].prompt, "api.c") != NULL);
    ASSERT(strstr(p.nodes[2].prompt, "db.c") != NULL);

    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

static int t_merge_batch(void) {
    static fanout_spec_t f;
    merge_spec_t m;
    static pipeline_t p;
    pipeline_init(&p, "mb", 8);

    fanout_init(&f, "w", "work");
    fanout_set_count(&f, 10);
    fanout_expand(&f, &p);

    merge_init(&m, "reducer", "w");
    merge_set_batch(&m, 4);
    int nb = merge_expand(&m, &f, &p, "merge results");
    ASSERT_INT_EQ(nb, 3);
    ASSERT_INT_EQ(p.nnodes, 13);

    ASSERT(p.nodes[10].ndeps > 0);

    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Success Criteria ═══════════ */

static int t_sc_output_contains(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "LGTM", false);

    char detail[256];
    ASSERT(sc_evaluate(&rs, "Looks good, LGTM!", 0, NULL, detail, sizeof(detail)));
    ASSERT(!sc_evaluate(&rs, "Needs work", 0, NULL, detail, sizeof(detail)));
    return 0;
}

static int t_sc_case_insensitive(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "pass", false);

    char detail[256];
    ASSERT(sc_evaluate(&rs, "All tests PASS", 0, NULL, detail, sizeof(detail)));
    return 0;
}

static int t_sc_exit_zero(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_exit_zero(&rs);

    char detail[256];
    ASSERT(sc_evaluate(&rs, "output", 0, NULL, detail, sizeof(detail)));
    ASSERT(!sc_evaluate(&rs, "output", 1, NULL, detail, sizeof(detail)));
    return 0;
}

static int t_sc_conjunction(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "OK", false);
    sc_add_exit_zero(&rs);

    char detail[256];
    ASSERT(sc_evaluate(&rs, "OK done", 0, NULL, detail, sizeof(detail)));
    ASSERT(!sc_evaluate(&rs, "OK done", 1, NULL, detail, sizeof(detail)));
    ASSERT(!sc_evaluate(&rs, "nope", 0, NULL, detail, sizeof(detail)));
    return 0;
}

static int t_sc_not_contains(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_not_contains(&rs, "ERROR", false);

    char detail[256];
    ASSERT(sc_evaluate(&rs, "all good", 0, NULL, detail, sizeof(detail)));
    ASSERT(!sc_evaluate(&rs, "got an ERROR here", 0, NULL, detail, sizeof(detail)));
    return 0;
}

static int t_sc_file_exists(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_file_exists(&rs, "/tmp");

    char detail[256];
    ASSERT(sc_evaluate(&rs, "", 0, NULL, detail, sizeof(detail)));

    sc_ruleset_init(&rs);
    sc_add_file_exists(&rs, "/nonexistent_path_xyzzy");
    ASSERT(!sc_evaluate(&rs, "", 0, NULL, detail, sizeof(detail)));
    return 0;
}

static int t_sc_kind_names(void) {
    ASSERT_STR_EQ(sc_kind_name(SC_OUTPUT_CONTAINS), "output_contains");
    ASSERT_STR_EQ(sc_kind_name(SC_EXIT_CODE_ZERO), "exit_zero");
    ASSERT_STR_EQ(sc_kind_name(SC_FILE_EXISTS), "file_exists");
    return 0;
}

/* ═══════════ Cancel Scope ═══════════ */

static int t_cscope_basic(void) {
    cscope_t root;
    cscope_init(&root, "run", NULL);
    ASSERT(!cscope_is_cancelled(&root));
    ASSERT(!cscope_should_stop(&root));
    ASSERT_INT_EQ(cscope_depth(&root), 0);

    cscope_request_cancel(&root);
    ASSERT(cscope_is_cancelled(&root));
    ASSERT(cscope_should_stop(&root));

    cscope_reset(&root);
    ASSERT(!cscope_is_cancelled(&root));
    return 0;
}

static int t_cscope_hierarchy(void) {
    cscope_t root, child;
    cscope_init(&root, "run", NULL);
    cscope_init(&child, "node-1", &root);
    ASSERT_INT_EQ(cscope_depth(&child), 1);

    ASSERT(!cscope_should_stop(&child));
    cscope_request_cancel(&root);
    ASSERT(cscope_should_stop(&child));
    return 0;
}

static int t_cscope_child_only(void) {
    cscope_t root, child;
    cscope_init(&root, "run", NULL);
    cscope_init(&child, "node-1", &root);

    cscope_request_cancel(&child);
    ASSERT(cscope_should_stop(&child));
    ASSERT(!cscope_should_stop(&root));
    return 0;
}

/* ═══════════ Shared Pad ═══════════ */

static int t_spad_basic(void) {
    static shared_pad_t sp;
    shared_pad_init(&sp);

    ASSERT_INT_EQ(shared_pad_line_count(&sp), 0);
    shared_pad_append(&sp, "agent-1", "found bug in auth.c");
    shared_pad_append(&sp, "agent-2", "auth.c uses weak hash");
    ASSERT_INT_EQ(shared_pad_line_count(&sp), 2);

    char *text = shared_pad_read(&sp);
    ASSERT_NOT_NULL(text);
    ASSERT(strstr(text, "found bug") != NULL);
    ASSERT(strstr(text, "weak hash") != NULL);
    free(text);

    shared_pad_free(&sp);
    return 0;
}

static int t_spad_dedup(void) {
    static shared_pad_t sp;
    shared_pad_init(&sp);

    shared_pad_merge(&sp, "a", "line1\nline2\nline3");
    ASSERT_INT_EQ(shared_pad_line_count(&sp), 3);

    int added = shared_pad_merge(&sp, "b", "line2\nline4");
    ASSERT_INT_EQ(added, 1);
    ASSERT_INT_EQ(shared_pad_line_count(&sp), 4);

    shared_pad_free(&sp);
    return 0;
}

static int t_spad_persist(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_spad_%d.md", (int)getpid());

    static shared_pad_t sp;
    shared_pad_init_persistent(&sp, path);
    shared_pad_append(&sp, "a1", "finding one");
    shared_pad_append(&sp, "a2", "finding two");
    shared_pad_persist(&sp);
    shared_pad_free(&sp);

    static shared_pad_t sp2;
    shared_pad_init_persistent(&sp2, path);
    shared_pad_load(&sp2);
    ASSERT(shared_pad_line_count(&sp2) >= 2);

    char *text = shared_pad_read(&sp2);
    ASSERT(strstr(text, "finding one") != NULL);
    free(text);

    shared_pad_free(&sp2);
    unlink(path);
    return 0;
}

static int t_spad_load_replaces_state(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_spad_reload_%d.md", (int)getpid());

    static shared_pad_t sp;
    shared_pad_init_persistent(&sp, path);
    ASSERT_INT_EQ(shared_pad_append(&sp, "disk-a", "finding one"), 0);
    ASSERT_INT_EQ(shared_pad_append(&sp, "disk-b", "finding two"), 0);
    shared_pad_free(&sp);

    static shared_pad_t sp2;
    shared_pad_init(&sp2);
    nc_strlcpy(sp2.path, path, sizeof(sp2.path));
    ASSERT_INT_EQ(shared_pad_append(&sp2, "mem", "memory only"), 0);
    ASSERT_INT_EQ(shared_pad_line_count(&sp2), 1);

    ASSERT_INT_EQ(shared_pad_load(&sp2), 0);
    ASSERT_INT_EQ(shared_pad_line_count(&sp2), 2);

    char *text = shared_pad_read(&sp2);
    ASSERT_NOT_NULL(text);
    ASSERT(strstr(text, "[disk-a] finding one") != NULL);
    ASSERT(strstr(text, "[disk-b] finding two") != NULL);
    ASSERT(strstr(text, "memory only") == NULL);
    free(text);

    ASSERT_INT_EQ(shared_pad_load(&sp2), 0);
    ASSERT_INT_EQ(shared_pad_line_count(&sp2), 2);
    text = shared_pad_read(&sp2);
    ASSERT_NOT_NULL(text);
    ASSERT(strstr(text, "[disk-a] finding one") != NULL);
    ASSERT(strstr(text, "[disk-b] finding two") != NULL);
    free(text);

    shared_pad_free(&sp2);
    unlink(path);
    return 0;
}

static int t_spad_persist_run_token_metadata(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_spad_token_%d.md", (int)getpid());

    static shared_pad_t sp;
    shared_pad_init_persistent(&sp, path);
    shared_pad_set_run_token(&sp, "spad-run-token");
    ASSERT_INT_EQ(shared_pad_append(&sp, "a1", "finding one"), 0);

    char *raw = strbuf_read_file_text(path, NULL);
    ASSERT_NOT_NULL(raw);
    ASSERT(strstr(raw, SPAD_META_MAGIC) != NULL);
    ASSERT(strstr(raw, "spad-run-token") != NULL);
    free(raw);
    shared_pad_free(&sp);

    static shared_pad_t sp2;
    shared_pad_init_persistent(&sp2, path);
    ASSERT_INT_EQ(shared_pad_load(&sp2), 0);
    ASSERT_STR_EQ(sp2.current_run_token, "spad-run-token");
    ASSERT_INT_EQ(shared_pad_line_count(&sp2), 1);

    char *text = shared_pad_read(&sp2);
    ASSERT_NOT_NULL(text);
    ASSERT(strstr(text, SPAD_META_MAGIC) == NULL);
    ASSERT(strstr(text, "[a1] finding one") != NULL);
    free(text);

    shared_pad_free(&sp2);
    unlink(path);
    return 0;
}

/* ═══════════ Event Ledger ═══════════ */

static int t_ledger_basic(void) {
    static event_ledger_t el;
    ledger_init(&el);

    int s1 = ledger_append(&el, "start", NULL, "pipeline began");
    ASSERT_INT_EQ(s1, 1);
    int s2 = ledger_append(&el, "node_done", "plan", "ok");
    ASSERT_INT_EQ(s2, 2);
    ASSERT_INT_EQ(ledger_event_count(&el), 2);

    const ledger_event_t *ev = ledger_event_at(&el, 0);
    ASSERT_NOT_NULL(ev);
    ASSERT_STR_EQ(ev->type, "start");

    char *tail = ledger_format_tail(&el, 5);
    ASSERT_NOT_NULL(tail);
    ASSERT(strstr(tail, "start") != NULL);
    free(tail);

    ledger_free(&el);
    return 0;
}

static int t_ledger_persist(void) {
    char log[256], snap[256];
    snprintf(log, sizeof(log), "/tmp/nc_test_ledger_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_snap_%d.json", (int)getpid());
    char detail[LEDGER_DETAIL_MAX];
    const char *output = "line one\nline two";
    for (int i = 0; i < LEDGER_DETAIL_MAX - 2; i++)
        detail[i] = (i % 23 == 0) ? '\n' : '"';
    detail[LEDGER_DETAIL_MAX - 2] = 'X';
    detail[LEDGER_DETAIL_MAX - 1] = '\0';

    static event_ledger_t el;
    ledger_init_persistent(&el, log, snap);
    ledger_append(&el, "ev1", "n1", "detail1");
    ledger_append_result(&el, "ev2", "n2", detail, output, 7);
    ledger_write_snapshot(&el, "{\"state\":\"ok\"}");
    ledger_free(&el);

    static event_ledger_t el2;
    ledger_init_persistent(&el2, log, snap);
    int loaded = ledger_load_log(&el2);
    ASSERT_INT_EQ(loaded, 2);
    ASSERT_STR_EQ(el2.events[0].type, "ev1");
    ASSERT_STR_EQ(el2.events[1].detail, detail);
    ASSERT(el2.events[1].has_output);
    ASSERT_STR_EQ(el2.events[1].output, output);
    ASSERT(el2.events[1].has_exit_code);
    ASSERT_INT_EQ(el2.events[1].exit_code, 7);
    ASSERT_INT_EQ(ledger_event_count(&el2), 2);

    loaded = ledger_load_log(&el2);
    ASSERT_INT_EQ(loaded, 2);
    ASSERT_INT_EQ(ledger_event_count(&el2), 2);
    ASSERT_STR_EQ(el2.events[1].detail, detail);
    ASSERT(el2.events[1].has_output);
    ASSERT_STR_EQ(el2.events[1].output, output);
    ASSERT(el2.events[1].has_exit_code);
    ASSERT_INT_EQ(el2.events[1].exit_code, 7);
    ledger_free(&el2);

    unlink(log);
    unlink(snap);
    return 0;
}

static int t_ledger_since(void) {
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

/* ═══════════ Preflight Report ═══════════ */

static int t_pflt_basic(void) {
    static pipeline_t p;
    pipeline_init(&p, "test-pf", 4);
    pipeline_add_node(&p, "plan", "make a plan");
    pipeline_add_node(&p, "impl", "implement");
    pipeline_add_dep(&p, "impl", "plan");

    pflt_report_t r;
    pflt_build_from_pipeline(&r, &p);
    ASSERT_INT_EQ(r.nsteps, 2);
    ASSERT(!r.has_cycles);

    char *report = pflt_format_report(&r);
    ASSERT_NOT_NULL(report);
    ASSERT(strstr(report, "plan") != NULL);
    ASSERT(strstr(report, "impl") != NULL);
    free(report);

    pipeline_free(&p);
    return 0;
}

static int t_pflt_warnings(void) {
    static pipeline_t p;
    pipeline_init(&p, "warn-pf", 4);
    pipeline_add_node(&p, "solo", "do something alone");

    pflt_report_t r;
    pflt_build_from_pipeline(&r, &p);
    /* Single-node pipeline has no isolation warning (nnodes == 1) */
    ASSERT_INT_EQ(r.nwarnings, 0);

    char *summary = pflt_format_summary(&r);
    ASSERT_NOT_NULL(summary);
    free(summary);

    pipeline_free(&p);
    return 0;
}

/* ═══════════ Periodic Probe ═══════════ */

static int t_probe_basic(void) {
    periodic_probe_t pp;
    probe_init(&pp, "workers", 10);
    ASSERT_INT_EQ(pp.tick_count, 0);

    probe_record_tick(&pp);
    ASSERT_INT_EQ(pp.tick_count, 1);
    ASSERT_INT_EQ(pp.state, PROBE_WATCHING);

    probe_add_action(&pp, PROBE_ACT_CANCEL, "workers_03", "stalled");
    ASSERT_INT_EQ(pp.nactions, 1);

    static pipeline_t p;
    pipeline_init(&p, "probe-test", 4);
    pipeline_add_node(&p, "workers_03", "work");
    pipeline_mark_running(&p, 0);

    int applied = probe_apply_actions(&pp, &p);
    ASSERT_INT_EQ(applied, 1);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_CANCELLED);

    char *s = probe_format_status(&pp);
    ASSERT_NOT_NULL(s);
    free(s);

    probe_free(&pp);
    pipeline_free(&p);
    return 0;
}

static int t_probe_settled(void) {
    periodic_probe_t pp;
    probe_init(&pp, "w", 10);

    static pipeline_t p;
    pipeline_init(&p, "settled", 4);
    pipeline_add_node(&p, "w_0", "work");
    pipeline_add_node(&p, "w_1", "work");
    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "ok", 0);
    pipeline_mark_running(&p, 1);
    pipeline_mark_done(&p, 1, "ok", 0);

    ASSERT(probe_group_settled(&pp, &p));

    probe_free(&pp);
    pipeline_free(&p);
    return 0;
}

static int t_probe_snapshot_restore_timing(void) {
    periodic_probe_t pp;
    probe_init(&pp, "workers", 2);
    probe_record_tick(&pp);
    ASSERT_INT_EQ(probe_add_action(&pp, PROBE_ACT_RESTART, "workers_0", "resume"), 0);

    probe_snapshot_t snap;
    probe_snapshot(&pp, &snap);
    ASSERT_STR_EQ(snap.watch_group, "workers");
    ASSERT_INT_EQ(snap.interval_s, 2);
    ASSERT_INT_EQ(snap.tick_count, 1);
    ASSERT_INT_EQ(snap.state, PROBE_WATCHING);
    ASSERT_INT_EQ(snap.nactions, 1);
    ASSERT(snap.next_tick_delay_ms > 0);

    snap.last_tick_age_ms = 25;
    snap.next_tick_delay_ms = 40;

    periodic_probe_t restored;
    probe_restore(&restored, &snap);
    ASSERT_STR_EQ(restored.watch_group, "workers");
    ASSERT_INT_EQ(restored.interval_s, 2);
    ASSERT_INT_EQ(restored.tick_count, 1);
    ASSERT_INT_EQ(restored.state, PROBE_WATCHING);
    ASSERT_INT_EQ(restored.nactions, 1);
    ASSERT_INT_EQ(restored.actions[0].kind, PROBE_ACT_RESTART);
    ASSERT_STR_EQ(restored.actions[0].target_id, "workers_0");
    ASSERT_STR_EQ(restored.actions[0].reason, "resume");
    ASSERT(restored.last_tick_ms > 0);
    ASSERT_FALSE(probe_should_tick(&restored, nc_mono_ms()));
    usleep(70000);
    ASSERT_TRUE(probe_should_tick(&restored, nc_mono_ms()));

    probe_free(&pp);
    probe_free(&restored);
    return 0;
}

/* ═══════════ Retry Cycle ═══════════ */

static int t_rcycle_basic(void) {
    retry_cycle_t rc;
    rcycle_init(&rc, 3);

    const char *targets[] = {"write"};
    rcycle_add_edge(&rc, "review", targets, 1);

    ASSERT(rcycle_should_restart(&rc, "review"));
    ASSERT(!rcycle_should_restart(&rc, "unknown"));
    ASSERT_INT_EQ(rcycle_iteration(&rc, "review"), 0);

    static pipeline_t p;
    pipeline_init(&p, "cycle-test", 4);
    pipeline_add_node(&p, "write", "pw");
    pipeline_add_node(&p, "review", "pr");
    pipeline_add_dep(&p, "review", "write");

    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "code", 0);
    pipeline_mark_running(&p, 1);
    pipeline_mark_failed(&p, 1, "needs fixes", 1);

    int iter = rcycle_do_restart(&rc, "review", &p);
    ASSERT_INT_EQ(iter, 1);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_PENDING);
    ASSERT_INT_EQ(p.nodes[1].status, PIPE_PENDING);

    rcycle_do_restart(&rc, "review", &p);
    rcycle_do_restart(&rc, "review", &p);
    ASSERT(rcycle_exhausted(&rc, "review"));
    ASSERT(!rcycle_should_restart(&rc, "review"));

    char *s = rcycle_format_status(&rc);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "3/3") != NULL);
    free(s);

    rcycle_free(&rc);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Orchestrator ═══════════ */

static int t_orch_full_run(void) {
    static pipeline_t p;
    pipeline_init(&p, "orch-test", 4);
    pipeline_add_node(&p, "plan", "plan the work");
    pipeline_add_node(&p, "impl", "implement");
    pipeline_add_node(&p, "test", "run tests");
    pipeline_add_dep(&p, "impl", "plan");
    pipeline_add_dep(&p, "test", "impl");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);

    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT(pipe_orch_is_running(&o));

    int ready[8];
    int nr = pipe_orch_step(&o, ready, 8);
    ASSERT_INT_EQ(nr, 1);
    pipe_orch_complete_node(&o, "plan", "the plan", 0);

    nr = pipe_orch_step(&o, ready, 8);
    ASSERT_INT_EQ(nr, 1);
    pipe_orch_complete_node(&o, "impl", "the code", 0);

    nr = pipe_orch_step(&o, ready, 8);
    ASSERT_INT_EQ(nr, 1);
    pipe_orch_complete_node(&o, "test", "all pass", 0);

    pipe_orch_step(&o, ready, 8);
    ASSERT(pipe_orch_is_terminal(&o));
    ASSERT_INT_EQ(o.state, ORCH_DONE);

    char *outputs = pipe_orch_collect_outputs(&o);
    ASSERT_NOT_NULL(outputs);
    ASSERT(strstr(outputs, "the plan") != NULL);
    ASSERT(strstr(outputs, "the code") != NULL);
    free(outputs);

    ASSERT(ledger_event_count(&o.ledger) >= 4);

    char *status = pipe_orch_format_status(&o);
    ASSERT_NOT_NULL(status);
    ASSERT(strstr(status, "done") != NULL);
    free(status);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_orch_with_criteria(void) {
    static pipeline_t p;
    pipeline_init(&p, "criteria", 2);
    pipeline_add_node(&p, "check", "verify output");

    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "PASS", false);

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_add_criteria(&o, "check", &rs);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);

    int rc = pipe_orch_complete_node(&o, "check", "FAIL output", 0);
    ASSERT_INT_EQ(rc, -1);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_FAILED);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_orch_with_cycle(void) {
    static pipeline_t p;
    pipeline_init(&p, "cycle-orch", 2);
    p.max_iterations = 3;
    pipeline_add_node(&p, "write", "write code");
    pipeline_add_node(&p, "review", "review code");
    pipeline_add_dep(&p, "review", "write");

    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "LGTM", true);

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_add_criteria(&o, "review", &rs);
    const char *targets[] = {"write"};
    pipe_orch_add_cycle(&o, "review", targets, 1);
    pipe_orch_begin(&o);

    int ready[8];

    /* Iteration 1: write succeeds, review fails -> cycle */
    pipe_orch_step(&o, ready, 8);
    pipe_orch_complete_node(&o, "write", "first draft", 0);
    pipe_orch_step(&o, ready, 8);
    int rc = pipe_orch_complete_node(&o, "review", "needs work", 0);
    ASSERT_INT_EQ(rc, 1);

    /* Iteration 2: write succeeds, review passes */
    pipe_orch_step(&o, ready, 8);
    pipe_orch_complete_node(&o, "write", "second draft", 0);
    pipe_orch_step(&o, ready, 8);
    rc = pipe_orch_complete_node(&o, "review", "LGTM!", 0);
    ASSERT_INT_EQ(rc, 0);

    pipe_orch_step(&o, ready, 8);
    ASSERT(pipe_orch_is_terminal(&o));
    ASSERT_INT_EQ(o.state, ORCH_DONE);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_orch_cancel(void) {
    static pipeline_t p;
    pipeline_init(&p, "cancel-orch", 2);
    pipeline_add_node(&p, "a", "pa");
    pipeline_add_node(&p, "b", "pb");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    pipe_orch_cancel(&o);
    pipe_orch_step(&o, ready, 8);

    ASSERT(pipe_orch_is_terminal(&o));
    ASSERT_INT_EQ(o.state, ORCH_CANCELLED);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_orch_pad(void) {
    static pipeline_t p;
    pipeline_init(&p, "pad-orch", 2);
    pipeline_add_node(&p, "a", "pa");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);

    pipe_orch_pad_write(&o, "agent-1", "found issue in main.c");
    pipe_orch_pad_write(&o, "agent-2", "confirmed issue\nfound issue in main.c");

    char *text = pipe_orch_pad_read(&o);
    ASSERT_NOT_NULL(text);
    ASSERT(strstr(text, "found issue") != NULL);
    ASSERT(strstr(text, "confirmed issue") != NULL);
    free(text);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Scope Aggregate ═══════════ */

static int t_scope_collect(void) {
    static pipeline_t p;
    pipeline_init(&p, "scope-test", 4);

    static fanout_spec_t f;
    fanout_init(&f, "sweep", "review shard");
    fanout_set_count(&f, 3);
    fanout_expand(&f, &p);

    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "found bug A", 0);
    pipeline_mark_running(&p, 1);
    pipeline_mark_failed(&p, 1, "error", 1);
    pipeline_mark_running(&p, 2);
    pipeline_mark_done(&p, 2, "found bug B", 0);

    scope_aggregate_t sa;
    scope_agg_init(&sa, "sweep");
    int n = scope_agg_collect(&sa, &p, &f);
    ASSERT_INT_EQ(n, 3);
    ASSERT_INT_EQ(sa.summary.total, 3);
    ASSERT_INT_EQ(sa.summary.completed, 2);
    ASSERT_INT_EQ(sa.summary.failed, 1);
    ASSERT_INT_EQ(sa.summary.with_output, 3);

    ASSERT_INT_EQ(scope_agg_count_with_output(&sa), 3);
    ASSERT_INT_EQ(scope_agg_count_by_status(&sa, PIPE_DONE), 2);
    ASSERT_INT_EQ(scope_agg_count_by_status(&sa, PIPE_FAILED), 1);

    char *outputs = scope_agg_joined_outputs(&sa);
    ASSERT_NOT_NULL(outputs);
    ASSERT(strstr(outputs, "found bug A") != NULL);
    ASSERT(strstr(outputs, "found bug B") != NULL);
    free(outputs);

    char *filtered = scope_agg_filtered_outputs(&sa, PIPE_FAILED);
    ASSERT_NOT_NULL(filtered);
    ASSERT(strstr(filtered, "sweep_1") != NULL);
    free(filtered);

    char *fmt = scope_agg_format(&sa);
    ASSERT_NOT_NULL(fmt);
    ASSERT(strstr(fmt, "completed=2") != NULL);
    free(fmt);

    char *ctx = scope_agg_build_ctx(&sa);
    ASSERT_NOT_NULL(ctx);
    ASSERT(strstr(ctx, "Failed members") != NULL);
    free(ctx);

    scope_agg_free(&sa);
    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

static int t_scope_empty(void) {
    scope_aggregate_t sa;
    scope_agg_init(&sa, "empty");
    ASSERT_INT_EQ(sa.nmembers, 0);
    ASSERT_INT_EQ(scope_agg_count_with_output(&sa), 0);

    scope_summary_t s = scope_agg_summarize(&sa);
    ASSERT_INT_EQ(s.total, 0);

    scope_agg_free(&sa);
    return 0;
}

/* ═══════════ Prompt Context ═══════════ */

static int t_pctx_render(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);

    prompt_ctx_set(&pc, "name", "Alice");
    prompt_ctx_set(&pc, "task", "review code");

    char *r = prompt_ctx_render(&pc, "Hello {{ name }}, please {{ task }}.");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "Hello Alice") != NULL);
    ASSERT(strstr(r, "please review code") != NULL);
    free(r);

    r = prompt_ctx_render(&pc, "No vars here.");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "No vars here.");
    free(r);

    r = prompt_ctx_render(&pc, "Unknown {{ missing }} kept.");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "{{missing}}") != NULL);
    free(r);

    prompt_ctx_free(&pc);
    return 0;
}

static int t_pctx_pipeline(void) {
    static pipeline_t p;
    pipeline_init(&p, "ctx-test", 2);
    pipeline_add_node(&p, "prep", "prepare");
    pipeline_add_node(&p, "exec", "run {{ prep.output }}");
    pipeline_add_dep(&p, "exec", "prep");

    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "instructions here", 0);

    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    prompt_ctx_populate(&pc, &p);

    ASSERT_STR_EQ(prompt_ctx_get(&pc, "prep.output"), "instructions here");
    ASSERT_STR_EQ(prompt_ctx_get(&pc, "prep.status"), "done");
    ASSERT_STR_EQ(prompt_ctx_get(&pc, "pipeline.name"), "ctx-test");

    char *rendered = prompt_ctx_render(&pc, p.nodes[1].prompt);
    ASSERT_NOT_NULL(rendered);
    ASSERT(strstr(rendered, "run instructions here") != NULL);
    free(rendered);

    prompt_ctx_free(&pc);
    pipeline_free(&p);
    return 0;
}

static int t_pctx_scope(void) {
    static pipeline_t p;
    pipeline_init(&p, "scope-ctx", 4);
    static fanout_spec_t f;
    fanout_init(&f, "workers", "do work");
    fanout_set_count(&f, 2);
    fanout_expand(&f, &p);
    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "result1", 0);
    pipeline_mark_running(&p, 1);
    pipeline_mark_done(&p, 1, "result2", 0);

    scope_aggregate_t sa;
    scope_agg_init(&sa, "workers");
    scope_agg_collect(&sa, &p, &f);

    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    prompt_ctx_populate_scope(&pc, "scope", &sa);

    ASSERT_STR_EQ(prompt_ctx_get(&pc, "scope.total"), "2");
    ASSERT_STR_EQ(prompt_ctx_get(&pc, "scope.completed"), "2");

    char *r = prompt_ctx_render(&pc, "Total: {{ scope.total }}, Done: {{ scope.completed }}");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "Total: 2") != NULL);
    ASSERT(strstr(r, "Done: 2") != NULL);
    free(r);

    prompt_ctx_free(&pc);
    scope_agg_free(&sa);
    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Node Artifact ═══════════ */

static int t_artifact_basic(void) {
    static artifact_store_t as;
    artifact_store_init(&as, "/tmp/nc_test_artifacts");

    ASSERT_INT_EQ(artifact_store_ensure_node(&as, "nodeA"), 0);
    ASSERT_INT_EQ(as.nnodes, 1);

    int w = artifact_write(&as, "nodeA", "output.txt", "hello world");
    ASSERT(w > 0);

    char *content = artifact_read(&as, "nodeA", "output.txt");
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "hello world");
    free(content);

    artifact_append(&as, "nodeA", "stdout.log", "line 1\n");
    artifact_append(&as, "nodeA", "stdout.log", "line 2\n");
    content = artifact_read(&as, "nodeA", "stdout.log");
    ASSERT_NOT_NULL(content);
    ASSERT(strstr(content, "line 1") != NULL);
    ASSERT(strstr(content, "line 2") != NULL);
    free(content);

    ASSERT_INT_EQ(artifact_slot_count(&as, "nodeA"), 2);

    const char *p = artifact_path(&as, "nodeA", "output.txt");
    ASSERT_NOT_NULL(p);

    char *manifest = artifact_format_manifest(&as, "nodeA");
    ASSERT_NOT_NULL(manifest);
    ASSERT(strstr(manifest, "output.txt") != NULL);
    ASSERT(strstr(manifest, "stdout.log") != NULL);
    free(manifest);

    ASSERT(artifact_read(&as, "noexist", "x") == NULL);
    ASSERT(artifact_path(&as, "noexist", "x") == NULL);

    artifact_store_free(&as);
    return 0;
}

/* ═══════════ Ledger Watcher ═══════════ */

static int t_watcher_basic(void) {
    static event_ledger_t el;
    ledger_init(&el);

    ledger_watcher_t w1, w2;
    ledger_watcher_init(&w1, 1);
    ledger_watcher_init(&w2, 2);

    ASSERT_INT_EQ(ledger_subscribe(&el, &w1), 0);
    ASSERT_INT_EQ(ledger_subscribe(&el, &w2), 0);
    ASSERT_INT_EQ(el.nwatchers, 2);

    ledger_append(&el, "test_event", "n1", "detail1");
    ledger_append(&el, "test_event2", "n2", "detail2");

    ASSERT(ledger_watcher_has_events(&w1));
    ASSERT_INT_EQ(ledger_watcher_pending(&w1), 2);
    ASSERT_INT_EQ(ledger_watcher_pending(&w2), 2);

    ledger_event_t buf[8];
    int n = ledger_watcher_poll(&w1, buf, 8);
    ASSERT_INT_EQ(n, 2);
    ASSERT_STR_EQ(buf[0].type, "test_event");
    ASSERT_STR_EQ(buf[1].type, "test_event2");
    ASSERT(!ledger_watcher_has_events(&w1));

    n = ledger_watcher_poll(&w2, buf, 1);
    ASSERT_INT_EQ(n, 1);
    ASSERT_STR_EQ(buf[0].type, "test_event");
    ASSERT_INT_EQ(ledger_watcher_pending(&w2), 1);

    ledger_unsubscribe(&el, &w1);
    ASSERT_INT_EQ(el.nwatchers, 1);

    ledger_append(&el, "after_unsub", NULL, NULL);
    ASSERT(!ledger_watcher_has_events(&w1));
    ASSERT_INT_EQ(ledger_watcher_pending(&w2), 2);

    ledger_unsubscribe(&el, &w2);
    ASSERT_INT_EQ(el.nwatchers, 0);

    ledger_free(&el);
    return 0;
}

static int t_watcher_overflow(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_watcher_t w;
    ledger_watcher_init(&w, 1);
    ledger_subscribe(&el, &w);

    for (int i = 0; i < LEDGER_WATCHER_QUEUE + 10; i++) {
        char type[32];
        snprintf(type, sizeof(type), "evt_%d", i);
        ledger_append(&el, type, NULL, NULL);
    }

    ASSERT_INT_EQ(ledger_watcher_pending(&w), LEDGER_WATCHER_QUEUE);

    ledger_event_t buf[8];
    int n = ledger_watcher_poll(&w, buf, 1);
    ASSERT_INT_EQ(n, 1);
    ASSERT(strstr(buf[0].type, "evt_") != NULL);

    ledger_unsubscribe(&el, &w);
    ledger_free(&el);
    return 0;
}

/* ═══════════ Run Persistence ═══════════ */

static int t_persist_snapshot(void) {
    static pipeline_t p;
    pipeline_init(&p, "persist-test", 4);
    pipeline_add_node(&p, "a", "do A");
    pipeline_add_node(&p, "b", "do B");
    pipeline_add_dep(&p, "b", "a");

    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "result-a", 0);

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "NCRUN") != NULL);
    ASSERT(strstr(json, "persist-test") != NULL);
    ASSERT(strstr(json, "\"a\"") != NULL);
    ASSERT(strstr(json, "\"ledger_events\"") != NULL);
    int ledger_n = ledger_event_count(&o.ledger);
    ASSERT(ledger_n > 0);

    static pipeline_t p2;
    pipeline_init(&p2, "persist-test", 4);
    pipeline_add_node(&p2, "a", "do A");
    pipeline_add_node(&p2, "b", "do B");
    pipeline_add_dep(&p2, "b", "a");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);

    int rc = run_persist_restore_json(&o2, &p2, json);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_DONE);
    ASSERT_STR_EQ(p2.name, "persist-test");
    ASSERT_INT_EQ(ledger_event_count(&o2.ledger), ledger_n);
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, 0)->type, ledger_event_at(&o.ledger, 0)->type);

    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_file(void) {
    static pipeline_t p;
    pipeline_init(&p, "file-persist", 2);
    pipeline_add_node(&p, "x", "do X");
    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "output-x", 0);

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    const char *path = "/tmp/nc_test_run.json";
    int rc = run_persist_save(&o, path);
    ASSERT_INT_EQ(rc, 0);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist", 2);
    pipeline_add_node(&p2, "x", "do X");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);

    rc = run_persist_load(&o2, &p2, path);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_DONE);

    unlink(path);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_file_ledger(void) {
    char path[256], log[256], snap[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_ledger_%d.json", (int)getpid());
    snprintf(log, sizeof(log), "/tmp/nc_test_run_ledger_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_run_ledger_snap_%d.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-ledger", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));
    ASSERT(ledger_event_count(&o.ledger) >= 4);

    int rc = run_persist_save(&o, path);
    ASSERT_INT_EQ(rc, 0);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-ledger", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    rc = run_persist_load(&o2, &p2, path);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(ledger_event_count(&o2.ledger), ledger_event_count(&o.ledger));
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, 0)->type, "run_started");
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, ledger_event_count(&o2.ledger) - 1)->type,
                  "run_completed");

    unlink(path);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_begin_clears_stale_ledger_sidecars(void) {
    char log[256], snap[256];
    snprintf(log, sizeof(log), "/tmp/nc_test_begin_stale_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_begin_stale_%d.snap.json", (int)getpid());

    ASSERT_INT_EQ(th_write_text(log, "{\"seq\":1,\"ts\":1,\"type\":\"stale_old_run\"}\n"), 0);
    ASSERT_INT_EQ(th_write_text(snap, "{\"stale\":true}\n"), 0);

    static pipeline_t p;
    pipeline_init(&p, "fresh-run", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);

    static event_ledger_t check;
    ledger_init_persistent(&check, log, snap);
    ASSERT_INT_EQ(ledger_load_log(&check), 1);
    ASSERT_INT_EQ(ledger_event_count(&check), 1);
    ASSERT_STR_EQ(ledger_event_at(&check, 0)->type, "run_started");
    ASSERT(access(snap, F_OK) != 0);

    ledger_free(&check);
    unlink(log);
    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_persist_json_ledger(void) {
    char log[256], snap[256];
    snprintf(log, sizeof(log), "/tmp/nc_test_json_ledger_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_json_ledger_snap_%d.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "json-persist-ledger", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));
    int n_ev = ledger_event_count(&o.ledger);
    ASSERT(n_ev >= 4);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);

    static pipeline_t p2;
    pipeline_init(&p2, "json-persist-ledger", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_restore_json(&o2, &p2, json), 0);
    ASSERT_INT_EQ(ledger_event_count(&o2.ledger), n_ev);
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, 0)->type, "run_started");
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, ledger_event_count(&o2.ledger) - 1)->type,
                  "run_completed");

    free(json);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_json_ledger_prefers_fresher_snapshot(void) {
    char log[256], snap[256];
    snprintf(log, sizeof(log), "/tmp/nc_test_json_ledger_fresh_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_json_ledger_fresh_snap_%d.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "json-persist-ledger-fresh", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));

    int disk_events = ledger_event_count(&o.ledger);
    ASSERT(disk_events >= 4);

    o.ledger.persistent = false;
    ASSERT(ledger_append(&o.ledger, "snapshot_only", NULL, "newer than disk log") > 0);
    o.ledger.persistent = true;
    ASSERT_INT_EQ(ledger_event_count(&o.ledger), disk_events + 1);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);

    static pipeline_t p2;
    pipeline_init(&p2, "json-persist-ledger-fresh", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_restore_json(&o2, &p2, json), 0);
    ASSERT_INT_EQ(ledger_event_count(&o2.ledger), ledger_event_count(&o.ledger));
    const ledger_event_t *last = ledger_event_at(&o2.ledger, ledger_event_count(&o2.ledger) - 1);
    ASSERT_NOT_NULL(last);
    ASSERT_STR_EQ(last->type, "snapshot_only");
    ASSERT_STR_EQ(last->detail, "newer than disk log");

    free(json);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_save_reports_companion_failure(void) {
    char path[256], log[256], snap[256], broken_dir[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_save_fail_%d.json", (int)getpid());
    snprintf(broken_dir, sizeof(broken_dir), "/tmp/nc_test_missing_persist_%d", (int)getpid());
    snprintf(log, sizeof(log), "%.243s/log.jsonl", broken_dir);
    snprintf(snap, sizeof(snap), "%.244s/snap.json", broken_dir);
    (void)th_remove_tree(broken_dir);

    static pipeline_t p;
    pipeline_init(&p, "save-fail", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));

    ASSERT_INT_EQ(run_persist_save(&o, path), -1);

    unlink(path);
    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_persist_file_pad_merges_fresher_disk(void) {
    char path[256], pad_path[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_pad_%d.json", (int)getpid());
    snprintf(pad_path, sizeof(pad_path), "/tmp/nc_test_run_pad_%d.md", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-pad", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, NULL, NULL, pad_path);
    ASSERT_INT_EQ(pipe_orch_pad_write(&o, "snap", "from snapshot"), 1);
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);
    ASSERT_INT_EQ(pipe_orch_pad_write(&o, "disk", "disk only"), 1);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-pad", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, NULL, NULL, pad_path);
    ASSERT_INT_EQ(run_persist_load(&o2, &p2, path), 0);
    ASSERT_INT_EQ(shared_pad_line_count(&o2.pad), 2);

    char *pad = pipe_orch_pad_read(&o2);
    ASSERT_NOT_NULL(pad);
    ASSERT(strstr(pad, "[snap] from snapshot") != NULL);
    ASSERT(strstr(pad, "[disk] disk only") != NULL);
    free(pad);

    static shared_pad_t disk;
    shared_pad_init_persistent(&disk, pad_path);
    ASSERT_INT_EQ(shared_pad_load(&disk), 0);
    ASSERT_INT_EQ(shared_pad_line_count(&disk), 2);
    pad = shared_pad_read(&disk);
    ASSERT_NOT_NULL(pad);
    ASSERT(strstr(pad, "[snap] from snapshot") != NULL);
    ASSERT(strstr(pad, "[disk] disk only") != NULL);
    free(pad);

    shared_pad_free(&disk);
    unlink(path);
    unlink(pad_path);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_file_pad_ignores_mismatched_run_identity(void) {
    char path[256], pad_path[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_pad_token_%d.json", (int)getpid());
    snprintf(pad_path, sizeof(pad_path), "/tmp/nc_test_run_pad_token_%d.md", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-pad-token", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, NULL, NULL, pad_path);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT(o.run_scope.token[0] != '\0');
    char first_token[sizeof(o.run_scope.token)];
    nc_strlcpy(first_token, o.run_scope.token, sizeof(first_token));
    ASSERT_INT_EQ(pipe_orch_pad_write(&o, "snap", "run one note"), 1);
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-pad-token", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, NULL, NULL, pad_path);
    ASSERT_INT_EQ(pipe_orch_begin(&o2), 0);
    ASSERT(o2.run_scope.token[0] != '\0');
    ASSERT(strcmp(first_token, o2.run_scope.token) != 0);
    ASSERT_INT_EQ(pipe_orch_pad_write(&o2, "other", "run two note"), 1);

    static pipeline_t p3;
    pipeline_init(&p3, "file-persist-pad-token", 1);
    pipeline_add_node(&p3, "solo", "run");

    static pipe_orch_t o3;
    pipe_orch_init_persistent(&o3, &p3, NULL, NULL, pad_path);
    ASSERT_INT_EQ(run_persist_load(&o3, &p3, path), 0);
    ASSERT_STR_EQ(o3.run_scope.token, first_token);
    ASSERT_STR_EQ(o3.pad.current_run_token, first_token);
    ASSERT_INT_EQ(shared_pad_line_count(&o3.pad), 1);

    char *pad = pipe_orch_pad_read(&o3);
    ASSERT_NOT_NULL(pad);
    ASSERT(strstr(pad, "[snap] run one note") != NULL);
    ASSERT(strstr(pad, "run two note") == NULL);
    free(pad);

    unlink(path);
    unlink(pad_path);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipe_orch_free(&o3);
    pipeline_free(&p);
    pipeline_free(&p2);
    pipeline_free(&p3);
    return 0;
}

static int t_persist_file_prefers_fresher_sidecar_snapshot(void) {
    char path[256], log[256], snap[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_sidecar_%d.json", (int)getpid());
    snprintf(log, sizeof(log), "/tmp/nc_test_run_sidecar_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_run_sidecar_%d.snap.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-sidecar", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));

    char *newer = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(newer);
    ASSERT_INT_EQ(th_write_text(snap, newer), 0);
    free(newer);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-sidecar", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_load(&o2, &p2, path), 0);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_DONE);
    ASSERT_NOT_NULL(p2.nodes[0].output);
    ASSERT_STR_EQ(p2.nodes[0].output, "ok");
    ASSERT_TRUE(pipe_orch_is_terminal(&o2));

    unlink(path);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_file_legacy_snapshot_ignores_tokened_sidecar(void) {
    char path[256], log[256], snap[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_legacy_sidecar_%d.json", (int)getpid());
    snprintf(log, sizeof(log), "/tmp/nc_test_run_legacy_sidecar_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_run_legacy_sidecar_%d.jsonl.snap", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-legacy-sidecar", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);

    char *legacy_json = strbuf_read_file_text(path, NULL);
    ASSERT_NOT_NULL(legacy_json);
    char *token_key = strstr(legacy_json, "\"run_scope_token\": ");
    ASSERT_NOT_NULL(token_key);
    char *token_line_end = strchr(token_key, '\n');
    ASSERT_NOT_NULL(token_line_end);
    memmove(token_key, token_line_end + 1, strlen(token_line_end + 1) + 1);
    ASSERT_INT_EQ(th_write_text(path, legacy_json), 0);
    free(legacy_json);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-legacy-sidecar", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o2), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o2, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o2));

    char *newer = run_persist_snapshot_json(&o2);
    ASSERT_NOT_NULL(newer);
    ASSERT_INT_EQ(th_write_text(snap, newer), 0);
    free(newer);

    static pipeline_t p3;
    pipeline_init(&p3, "file-persist-legacy-sidecar", 1);
    pipeline_add_node(&p3, "solo", "run");

    static pipe_orch_t o3;
    pipe_orch_init_persistent(&o3, &p3, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_load(&o3, &p3, path), 0);
    ASSERT_STR_EQ(o3.run_scope.token, p3.name);
    ASSERT_INT_EQ(o3.state, ORCH_RUNNING);
    /* pipeline stays PIPE_PENDING until pipe_orch_step starts a node */
    ASSERT_INT_EQ(p3.status, PIPE_PENDING);
    ASSERT_INT_EQ(p3.nodes[0].status, PIPE_PENDING);
    ASSERT_TRUE(!pipe_orch_is_terminal(&o3));

    unlink(path);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipe_orch_free(&o3);
    pipeline_free(&p);
    pipeline_free(&p2);
    pipeline_free(&p3);
    return 0;
}

static int t_persist_file_ignores_mismatched_run_identity(void) {
    char path[256], path2[256], log[256], snap[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_token_guard_%d.json", (int)getpid());
    snprintf(path2, sizeof(path2), "/tmp/nc_test_run_token_guard_new_%d.json", (int)getpid());
    snprintf(log, sizeof(log), "/tmp/nc_test_run_token_guard_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_run_token_guard_%d.snap.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-token-guard", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT(o.run_scope.token[0] != '\0');
    char first_token[sizeof(o.run_scope.token)];
    nc_strlcpy(first_token, o.run_scope.token, sizeof(first_token));
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-token-guard", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o2), 0);
    ASSERT(o2.run_scope.token[0] != '\0');
    ASSERT(strcmp(first_token, o2.run_scope.token) != 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o2, "solo", "new-run", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o2));
    ASSERT_INT_EQ(run_persist_save(&o2, path2), 0);

    static pipeline_t p3;
    pipeline_init(&p3, "file-persist-token-guard", 1);
    pipeline_add_node(&p3, "solo", "run");

    static pipe_orch_t o3;
    pipe_orch_init_persistent(&o3, &p3, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_load(&o3, &p3, path), 0);
    ASSERT_STR_EQ(o3.run_scope.token, first_token);
    ASSERT_INT_EQ(o3.state, ORCH_RUNNING);
    ASSERT_INT_EQ(p3.status, PIPE_PENDING);
    ASSERT_INT_EQ(p3.nodes[0].status, PIPE_PENDING);
    ASSERT_TRUE(!pipe_orch_is_terminal(&o3));

    unlink(path);
    unlink(path2);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipe_orch_free(&o3);
    pipeline_free(&p);
    pipeline_free(&p2);
    pipeline_free(&p3);
    return 0;
}

static int t_persist_file_replays_fresher_log_state(void) {
    char path[256], log[256], snap[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_log_replay_%d.json", (int)getpid());
    snprintf(log, sizeof(log), "/tmp/nc_test_run_log_replay_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_run_log_replay_%d.snap.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-log-replay", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-log-replay", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_load(&o2, &p2, path), 0);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_DONE);
    ASSERT_INT_EQ(p2.status, PIPE_DONE);
    ASSERT_NOT_NULL(p2.nodes[0].output);
    ASSERT_STR_EQ(p2.nodes[0].output, "ok");
    ASSERT_INT_EQ(p2.nodes[0].exit_code, 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o2));
    ASSERT_INT_EQ(o2.state, ORCH_DONE);
    ASSERT_INT_EQ(ledger_event_count(&o2.ledger), ledger_event_count(&o.ledger));

    unlink(path);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_file_ignores_tokenless_fresher_log(void) {
    char path[256], log[256], snap[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_log_tokenless_%d.json", (int)getpid());
    snprintf(log, sizeof(log), "/tmp/nc_test_run_log_tokenless_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_run_log_tokenless_%d.snap.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-log-tokenless", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));

    char *log_txt = strbuf_read_file_text(log, NULL);
    ASSERT_NOT_NULL(log_txt);
    strbuf_t sb;
    strbuf_init(&sb);
    const char *line = log_txt;
    while (line && *line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len > 0) {
            char *entry = malloc(len + 1);
            ASSERT_NOT_NULL(entry);
            memcpy(entry, line, len);
            entry[len] = '\0';
            cJSON *obj = cJSON_Parse(entry);
            free(entry);
            if (cJSON_IsObject(obj))
                cJSON_DeleteItemFromObjectCaseSensitive(obj, "run_token");
            char *printed = obj ? cJSON_PrintUnformatted(obj) : NULL;
            if (printed) {
                strbuf_append_str(&sb, printed);
                strbuf_append_str(&sb, "\n");
                free(printed);
            }
            cJSON_Delete(obj);
        }
        line = nl ? nl + 1 : NULL;
    }
    free(log_txt);
    char *rewritten = strbuf_detach(&sb);
    ASSERT_NOT_NULL(rewritten);
    ASSERT_INT_EQ(th_write_text(log, rewritten), 0);
    free(rewritten);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-log-tokenless", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_load(&o2, &p2, path), 0);
    ASSERT_STR_EQ(o2.run_scope.token, o.run_scope.token);
    ASSERT_INT_EQ(o2.state, ORCH_RUNNING);
    ASSERT_INT_EQ(p2.status, PIPE_PENDING);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_PENDING);
    ASSERT_TRUE(!pipe_orch_is_terminal(&o2));

    unlink(path);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_file_ignores_mismatched_events_inside_log(void) {
    char path[256], log[256], snap[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_log_mixed_%d.json", (int)getpid());
    snprintf(log, sizeof(log), "/tmp/nc_test_run_log_mixed_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_run_log_mixed_%d.snap.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-log-mixed", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));

    char *log_txt = strbuf_read_file_text(log, NULL);
    ASSERT_NOT_NULL(log_txt);
    strbuf_t sb;
    strbuf_init(&sb);
    const char *line = log_txt;
    while (line && *line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len > 0) {
            char *entry = malloc(len + 1);
            ASSERT_NOT_NULL(entry);
            memcpy(entry, line, len);
            entry[len] = '\0';
            cJSON *obj = cJSON_Parse(entry);
            free(entry);
            cJSON *type = obj ? cJSON_GetObjectItem(obj, "type") : NULL;
            if (cJSON_IsString(type) && type->valuestring &&
                (strcmp(type->valuestring, "node_done") == 0 ||
                 strcmp(type->valuestring, "run_completed") == 0)) {
                cJSON_ReplaceItemInObject(obj, "run_token", cJSON_CreateString("foreign-run"));
            }
            char *printed = obj ? cJSON_PrintUnformatted(obj) : NULL;
            if (printed) {
                strbuf_append_str(&sb, printed);
                strbuf_append_str(&sb, "\n");
                free(printed);
            }
            cJSON_Delete(obj);
        }
        line = nl ? nl + 1 : NULL;
    }
    free(log_txt);
    char *rewritten = strbuf_detach(&sb);
    ASSERT_NOT_NULL(rewritten);
    ASSERT_INT_EQ(th_write_text(log, rewritten), 0);
    free(rewritten);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-log-mixed", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_load(&o2, &p2, path), 0);
    ASSERT_STR_EQ(o2.run_scope.token, o.run_scope.token);
    ASSERT_INT_EQ(o2.state, ORCH_RUNNING);
    ASSERT_INT_EQ(p2.status, PIPE_RUNNING);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_PENDING);
    ASSERT_TRUE(p2.nodes[0].output == NULL);
    ASSERT_TRUE(!pipe_orch_is_terminal(&o2));
    ASSERT_INT_EQ(ledger_event_count(&o2.ledger), 2);
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, 0)->type, "run_started");
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, 1)->type, "node_started");

    unlink(path);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_file_ignores_tokenless_events_inside_tokened_log(void) {
    char path[256], log[256], snap[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_log_partial_token_%d.json", (int)getpid());
    snprintf(log, sizeof(log), "/tmp/nc_test_run_log_partial_token_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_run_log_partial_token_%d.snap.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-log-partial-token", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));

    char *log_txt = strbuf_read_file_text(log, NULL);
    ASSERT_NOT_NULL(log_txt);
    strbuf_t sb;
    strbuf_init(&sb);
    const char *line = log_txt;
    while (line && *line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len > 0) {
            char *entry = malloc(len + 1);
            ASSERT_NOT_NULL(entry);
            memcpy(entry, line, len);
            entry[len] = '\0';
            cJSON *obj = cJSON_Parse(entry);
            free(entry);
            cJSON *type = obj ? cJSON_GetObjectItem(obj, "type") : NULL;
            if (cJSON_IsString(type) && type->valuestring &&
                (strcmp(type->valuestring, "node_done") == 0 ||
                 strcmp(type->valuestring, "run_completed") == 0)) {
                cJSON_DeleteItemFromObjectCaseSensitive(obj, "run_token");
            }
            char *printed = obj ? cJSON_PrintUnformatted(obj) : NULL;
            if (printed) {
                strbuf_append_str(&sb, printed);
                strbuf_append_str(&sb, "\n");
                free(printed);
            }
            cJSON_Delete(obj);
        }
        line = nl ? nl + 1 : NULL;
    }
    free(log_txt);
    char *rewritten = strbuf_detach(&sb);
    ASSERT_NOT_NULL(rewritten);
    ASSERT_INT_EQ(th_write_text(log, rewritten), 0);
    free(rewritten);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-log-partial-token", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_load(&o2, &p2, path), 0);
    ASSERT_STR_EQ(o2.run_scope.token, o.run_scope.token);
    ASSERT_INT_EQ(o2.state, ORCH_RUNNING);
    ASSERT_INT_EQ(p2.status, PIPE_RUNNING);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_PENDING);
    ASSERT_TRUE(p2.nodes[0].output == NULL);
    ASSERT_TRUE(!pipe_orch_is_terminal(&o2));
    ASSERT_INT_EQ(ledger_event_count(&o2.ledger), 2);
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, 0)->type, "run_started");
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, 1)->type, "node_started");

    unlink(path);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_json_ignores_mismatched_events_inside_snapshot(void) {
    static pipeline_t p;
    pipeline_init(&p, "persist-json-mixed-events", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "ok", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);
    cJSON *root = cJSON_Parse(json);
    ASSERT_NOT_NULL(root);
    cJSON *events = cJSON_GetObjectItem(root, "ledger_events");
    ASSERT(cJSON_IsArray(events));
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, events) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (!cJSON_IsString(type) || !type->valuestring)
            continue;
        if (strcmp(type->valuestring, "node_done") == 0 ||
            strcmp(type->valuestring, "run_completed") == 0) {
            cJSON_ReplaceItemInObject(item, "run_token", cJSON_CreateString("foreign-run"));
        }
    }
    char *mixed_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(json);
    ASSERT_NOT_NULL(mixed_json);

    static pipeline_t p2;
    pipeline_init(&p2, "persist-json-mixed-events", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);
    ASSERT_INT_EQ(run_persist_restore_json(&o2, &p2, mixed_json), 0);
    ASSERT_STR_EQ(o2.run_scope.token, o.run_scope.token);
    ASSERT_INT_EQ(o2.state, ORCH_RUNNING);
    ASSERT_INT_EQ(p2.status, PIPE_PENDING);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_PENDING);
    ASSERT_TRUE(p2.nodes[0].output == NULL);
    ASSERT_TRUE(!pipe_orch_is_terminal(&o2));
    ASSERT_INT_EQ(ledger_event_count(&o2.ledger), 2);
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, 0)->type, "run_started");
    ASSERT_STR_EQ(ledger_event_at(&o2.ledger, 1)->type, "node_started");

    free(mixed_json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_file_replays_fail_fast_skip(void) {
    char path[256], log[256], snap[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_run_failfast_replay_%d.json", (int)getpid());
    snprintf(log, sizeof(log), "/tmp/nc_test_run_failfast_replay_%d.jsonl", (int)getpid());
    snprintf(snap, sizeof(snap), "/tmp/nc_test_run_failfast_replay_%d.snap.json", (int)getpid());

    static pipeline_t p;
    pipeline_init(&p, "file-persist-failfast-replay", 1);
    p.fail_fast = true;
    pipeline_add_node(&p, "a", "run a");
    pipeline_add_node(&p, "b", "run b");
    pipeline_add_dep(&p, "b", "a");

    static pipe_orch_t o;
    pipe_orch_init_persistent(&o, &p, log, snap, NULL);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT_INT_EQ(run_persist_save(&o, path), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "a", "boom", 1), -1);
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o));
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_FAILED);
    ASSERT_INT_EQ(p.nodes[1].status, PIPE_SKIPPED);

    static pipeline_t p2;
    pipeline_init(&p2, "file-persist-failfast-replay", 1);
    p2.fail_fast = true;
    pipeline_add_node(&p2, "a", "run a");
    pipeline_add_node(&p2, "b", "run b");
    pipeline_add_dep(&p2, "b", "a");

    static pipe_orch_t o2;
    pipe_orch_init_persistent(&o2, &p2, log, snap, NULL);
    ASSERT_INT_EQ(run_persist_load(&o2, &p2, path), 0);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_FAILED);
    ASSERT_INT_EQ(p2.nodes[1].status, PIPE_SKIPPED);
    ASSERT_NOT_NULL(p2.nodes[0].output);
    ASSERT_STR_EQ(p2.nodes[0].output, "boom");
    ASSERT_INT_EQ(p2.nodes[0].exit_code, 1);
    ASSERT_INT_EQ(p2.status, PIPE_FAILED);
    ASSERT_INT_EQ(o2.state, ORCH_FAILED);
    ASSERT_TRUE(pipe_orch_is_terminal(&o2));

    unlink(path);
    unlink(log);
    unlink(snap);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

/* ═══════════ Scope Prefix Collect ═══════════ */

static int t_scope_prefix(void) {
    static pipeline_t p;
    pipeline_init(&p, "prefix-test", 4);

    static fanout_spec_t f;
    fanout_init(&f, "scan", "review");
    fanout_set_count(&f, 3);
    fanout_expand(&f, &p);

    pipeline_add_node(&p, "merge", "collect");

    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "ok1", 0);
    pipeline_mark_running(&p, 1);
    pipeline_mark_done(&p, 1, "ok2", 0);
    pipeline_mark_running(&p, 2);
    pipeline_mark_failed(&p, 2, "err", 1);

    scope_aggregate_t sa;
    scope_agg_init(&sa, "scan");
    int n = scope_agg_collect_prefix(&sa, &p, "scan");
    ASSERT_INT_EQ(n, 3);
    ASSERT_INT_EQ(sa.summary.completed, 2);
    ASSERT_INT_EQ(sa.summary.failed, 1);

    n = scope_agg_collect_prefix(&sa, &p, "merge");
    ASSERT_INT_EQ(n, 1);

    n = scope_agg_collect_prefix(&sa, &p, "nonexistent");
    ASSERT_INT_EQ(n, 0);

    scope_agg_free(&sa);
    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Prompt Context Edge Cases ═══════════ */

static int t_pctx_edge(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    prompt_ctx_set(&pc, "x", "hello");

    char *r = prompt_ctx_render(&pc, "{{x}}");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "hello");
    free(r);

    r = prompt_ctx_render(&pc, "a{{x}}b{{x}}c");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "ahellobhelloc");
    free(r);

    r = prompt_ctx_render(&pc, "{{}}notavar");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "notavar") != NULL);
    free(r);

    r = prompt_ctx_render(&pc, "unclosed {{ x no close");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "unclosed") != NULL);
    free(r);

    ASSERT(prompt_ctx_render(&pc, NULL) == NULL);

    prompt_ctx_set(&pc, "x", "updated");
    r = prompt_ctx_render(&pc, "{{x}}");
    ASSERT_STR_EQ(r, "updated");
    free(r);

    prompt_ctx_free(&pc);
    return 0;
}

/* ═══════════ Orchestrator + Artifacts Integration ═══════════ */

static int t_orch_artifacts(void) {
    static pipeline_t p;
    pipeline_init(&p, "art-orch", 4);
    pipeline_add_node(&p, "a", "do A");
    pipeline_add_node(&p, "b", "do B");
    pipeline_add_dep(&p, "b", "a");

    static artifact_store_t as;
    artifact_store_init(&as, "/tmp/nc_test_orch_art");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_attach_artifacts(&o, &as);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    pipe_orch_complete_node(&o, "a", "result from A", 0);

    char *content = artifact_read(&as, "a", "output.txt");
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "result from A");
    free(content);

    pipe_orch_step(&o, ready, 8);

    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "PASS", true);
    pipe_orch_add_criteria(&o, "b", &rs);
    pipe_orch_complete_node(&o, "b", "no pass here", 0);

    content = artifact_read(&as, "b", "output.txt");
    ASSERT_NOT_NULL(content);
    free(content);

    pipe_orch_step(&o, ready, 8);
    ASSERT(pipe_orch_is_terminal(&o));

    pipe_orch_free(&o);
    artifact_store_free(&as);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Orchestrator + Prompt Rendering ═══════════ */

static int t_orch_render(void) {
    static pipeline_t p;
    pipeline_init(&p, "render-orch", 2);
    pipeline_add_node(&p, "prep", "generate plan");
    pipeline_add_node(&p, "exec", "implement {{ prep.output }}");
    pipeline_add_dep(&p, "exec", "prep");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_enable_ctx_rendering(&o);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    pipe_orch_complete_node(&o, "prep", "the master plan", 0);

    char *rendered = pipe_orch_render_prompt(&o, "exec");
    ASSERT_NOT_NULL(rendered);
    ASSERT(strstr(rendered, "implement the master plan") != NULL);
    free(rendered);

    ASSERT(pipe_orch_render_prompt(&o, "nonexistent") == NULL);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Run Persist Scoped Parsing ═══════════ */

static int t_persist_scoped(void) {
    static pipeline_t p;
    pipeline_init(&p, "scoped", 4);
    p.fail_fast = true;
    p.max_iterations = 7;
    pipeline_add_node(&p, "alpha", "do alpha");
    pipeline_add_node(&p, "beta", "do beta");
    pipeline_add_dep(&p, "beta", "alpha");
    pipeline_set_retries(&p, "beta", 4);
    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "out-alpha", 0);
    pipeline_mark_running(&p, 1);
    pipeline_mark_failed(&p, 1, "err-beta", 1);
    p.status = PIPE_FAILED;
    p.started_ms = 700;
    p.finished_ms = 900;

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    o.state = ORCH_FAILED;
    o.started_ms = 1000;
    o.finished_ms = 5000;

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);

    static pipeline_t p2;
    pipeline_init(&p2, "empty", 1);
    pipeline_add_node(&p2, "alpha", "do alpha");
    pipeline_add_node(&p2, "beta", "do beta");
    pipeline_add_dep(&p2, "beta", "alpha");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);

    int rc = run_persist_restore_json(&o2, &p2, json);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(p2.name, "scoped");
    ASSERT_INT_EQ(p2.max_iterations, 7);
    ASSERT(p2.fail_fast);
    ASSERT_INT_EQ(p2.status, PIPE_FAILED);
    ASSERT_INT_EQ(p2.started_ms, 700);
    ASSERT_INT_EQ(p2.finished_ms, 900);
    ASSERT_INT_EQ(o2.state, ORCH_FAILED);
    ASSERT_INT_EQ(o2.started_ms, 1000);
    ASSERT_INT_EQ(o2.finished_ms, 5000);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_DONE);
    ASSERT_INT_EQ(p2.nodes[1].status, PIPE_FAILED);
    ASSERT_INT_EQ(p2.nodes[1].exit_code, 1);
    ASSERT_INT_EQ(p2.nodes[1].retries, 4);

    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_outputs_and_pad(void) {
    static const char *prep_output = "plan {\n"
                                     "  \"mode\": \"fast\"\n"
                                     "}\n"
                                     "quote=\"ok\"";

    static pipeline_t p;
    pipeline_init(&p, "persist-outputs", 2);
    pipeline_add_node(&p, "prep", "prepare");
    pipeline_add_node(&p, "exec", "run {{ prep.output }}");
    pipeline_add_dep(&p, "exec", "prep");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    ASSERT_INT_EQ(pipe_orch_pad_write(&o, "scout", "note with {brace}\nquote \"two\""), 2);
    ASSERT_INT_EQ(pipe_orch_pad_write(&o, "lead", "quote: \"ok\""), 1);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "prep", prep_output, 0), 0);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "\\\"mode\\\"") != NULL);
    ASSERT(strstr(json, "\"shared_pad\"") != NULL);

    static pipeline_t p2;
    pipeline_init(&p2, "persist-outputs", 2);
    pipeline_add_node(&p2, "prep", "prepare");
    pipeline_add_node(&p2, "exec", "run {{ prep.output }}");
    pipeline_add_dep(&p2, "exec", "prep");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);
    ASSERT_INT_EQ(run_persist_restore_json(&o2, &p2, json), 0);

    ASSERT_NOT_NULL(p2.nodes[0].output);
    ASSERT_STR_EQ(p2.nodes[0].output, prep_output);

    char *rendered = pipe_orch_render_prompt(&o2, "exec");
    ASSERT_NOT_NULL(rendered);
    ASSERT(strstr(rendered, "run plan {") != NULL);
    ASSERT(strstr(rendered, "\"mode\": \"fast\"") != NULL);
    ASSERT(strstr(rendered, "quote=\"ok\"") != NULL);
    ASSERT(strstr(rendered, "[scout] note with {brace}") != NULL);
    ASSERT(strstr(rendered, "quote \"two\"") != NULL);
    ASSERT(strstr(rendered, "[lead] quote: \"ok\"") != NULL);

    char *pad = pipe_orch_pad_read(&o2);
    ASSERT_NOT_NULL(pad);
    ASSERT(strstr(pad, "[scout] note with {brace}") != NULL);
    ASSERT(strstr(pad, "quote \"two\"") != NULL);
    ASSERT(strstr(pad, "[lead] quote: \"ok\"") != NULL);

    char *outputs = pipe_orch_collect_outputs(&o2);
    ASSERT_NOT_NULL(outputs);
    ASSERT(strstr(outputs, "## prep") != NULL);
    ASSERT(strstr(outputs, "\"mode\": \"fast\"") != NULL);

    free(outputs);
    free(pad);
    free(rendered);
    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

/* ═══════════ Watcher Subscribe/Unsubscribe Idempotence ═══════════ */

static int t_watcher_idempotent(void) {
    static event_ledger_t el;
    ledger_init(&el);
    ledger_watcher_t w;
    ledger_watcher_init(&w, 1);

    ASSERT_INT_EQ(ledger_subscribe(&el, &w), 0);
    ASSERT_INT_EQ(ledger_subscribe(&el, &w), 0);
    ASSERT_INT_EQ(el.nwatchers, 1);

    ledger_unsubscribe(&el, &w);
    ASSERT_INT_EQ(el.nwatchers, 0);
    ledger_unsubscribe(&el, &w);
    ASSERT_INT_EQ(el.nwatchers, 0);

    ledger_free(&el);
    return 0;
}

/* ═══════════ Orchestrator Node Cancel/Rerun ═══════════ */

static int t_orch_node_cancel_flag(void) {
    static pipeline_t p;
    pipeline_init(&p, "ncancel", 4);
    pipeline_add_node(&p, "a", "pa");
    pipeline_add_node(&p, "b", "pb");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    ASSERT(!pipe_orch_node_cancel_pending(&o, "a"));
    ASSERT_INT_EQ(pipe_orch_flag_node_cancel(&o, "a"), 0);
    ASSERT(pipe_orch_node_cancel_pending(&o, "a"));
    ASSERT(!pipe_orch_node_cancel_pending(&o, "b"));

    ASSERT_INT_EQ(pipe_orch_flag_node_cancel(&o, "a"), 0);
    ASSERT_INT_EQ(o.ncancel_flags, 1);

    pipe_orch_clear_node_cancel(&o, "a");
    ASSERT(!pipe_orch_node_cancel_pending(&o, "a"));
    ASSERT_INT_EQ(o.ncancel_flags, 0);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_orch_rerun_queue(void) {
    static pipeline_t p;
    pipeline_init(&p, "rerun", 4);
    pipeline_add_node(&p, "x", "px");
    pipeline_add_node(&p, "y", "py");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    pipe_orch_complete_node(&o, "x", "ok", 0);
    pipe_orch_complete_node(&o, "y", "ok", 0);

    ASSERT_INT_EQ(p.nodes[0].status, PIPE_DONE);
    ASSERT_INT_EQ(pipe_orch_queue_rerun(&o, "x"), 0);
    ASSERT_INT_EQ(pipe_orch_rerun_depth(&o), 1);

    ASSERT_INT_EQ(pipe_orch_queue_rerun(&o, "x"), 0);
    ASSERT_INT_EQ(pipe_orch_rerun_depth(&o), 1);

    int drained = pipe_orch_drain_reruns(&o);
    ASSERT_INT_EQ(drained, 1);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_PENDING);
    ASSERT_INT_EQ(pipe_orch_rerun_depth(&o), 0);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_orch_rerun_defers_running(void) {
    static pipeline_t p;
    pipeline_init(&p, "defer", 4);
    pipeline_add_node(&p, "w", "pw");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_RUNNING);

    pipe_orch_queue_rerun(&o, "w");
    int drained = pipe_orch_drain_reruns(&o);
    ASSERT_INT_EQ(drained, 0);
    ASSERT_INT_EQ(pipe_orch_rerun_depth(&o), 1);

    pipe_orch_complete_node(&o, "w", "ok", 0);
    drained = pipe_orch_drain_reruns(&o);
    ASSERT_INT_EQ(drained, 1);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_PENDING);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_orch_step_cancel_flag(void) {
    static pipeline_t p;
    pipeline_init(&p, "step-cancel", 4);
    pipeline_add_node(&p, "q", "pq");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_RUNNING);

    pipe_orch_flag_node_cancel(&o, "q");
    pipe_orch_step(&o, ready, 8);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_CANCELLED);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_orch_step_cancel_pending(void) {
    static pipeline_t p;
    pipeline_init(&p, "step-cancel-pending", 4);
    pipeline_add_node(&p, "q", "pq");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);
    ASSERT_INT_EQ(pipe_orch_flag_node_cancel(&o, "q"), 0);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 0);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_CANCELLED);

    for (int i = 0; i < ledger_event_count(&o.ledger); i++) {
        const ledger_event_t *ev = ledger_event_at(&o.ledger, i);
        ASSERT_NOT_NULL(ev);
        ASSERT(strcmp(ev->type, "node_started") != 0);
    }

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Scope Aggregate Subset/Partition ═══════════ */

static int t_scope_subset(void) {
    static pipeline_t p;
    pipeline_init(&p, "subset-test", 4);
    static fanout_spec_t f;
    fanout_init(&f, "s", "do work");
    fanout_set_count(&f, 4);
    fanout_expand(&f, &p);

    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "ok1", 0);
    pipeline_mark_running(&p, 1);
    pipeline_mark_failed(&p, 1, "err", 1);
    pipeline_mark_running(&p, 2);
    pipeline_mark_done(&p, 2, "ok2", 0);
    pipeline_mark_running(&p, 3);
    pipeline_mark_failed(&p, 3, "err2", 1);

    scope_aggregate_t sa, done, failed;
    scope_agg_init(&sa, "s");
    scope_agg_collect(&sa, &p, &f);
    ASSERT_INT_EQ(sa.nmembers, 4);

    scope_agg_init(&done, "s");
    int n = scope_agg_subset(&done, &sa, PIPE_DONE);
    ASSERT_INT_EQ(n, 2);
    ASSERT_INT_EQ(done.summary.completed, 2);

    scope_agg_init(&failed, "s");
    n = scope_agg_subset(&failed, &sa, PIPE_FAILED);
    ASSERT_INT_EQ(n, 2);
    ASSERT_INT_EQ(failed.summary.failed, 2);

    scope_agg_free(&done);
    scope_agg_free(&failed);
    scope_agg_free(&sa);
    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

static int t_scope_partition(void) {
    static pipeline_t p;
    pipeline_init(&p, "part-test", 4);
    static fanout_spec_t f;
    fanout_init(&f, "p", "work");
    fanout_set_count(&f, 3);
    fanout_expand(&f, &p);

    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "output", 0);
    pipeline_mark_running(&p, 1);
    pipeline_mark_done(&p, 1, "", 0);
    pipeline_mark_running(&p, 2);
    pipeline_mark_done(&p, 2, "data", 0);

    scope_aggregate_t sa, with_out, without_out;
    scope_agg_init(&sa, "p");
    scope_agg_collect(&sa, &p, &f);

    scope_agg_init(&with_out, "p");
    scope_agg_init(&without_out, "p");
    scope_agg_partition_output(&sa, &with_out, &without_out);

    ASSERT_INT_EQ(with_out.nmembers, 2);
    ASSERT_INT_EQ(without_out.nmembers, 1);

    scope_agg_free(&with_out);
    scope_agg_free(&without_out);
    scope_agg_free(&sa);
    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Artifact Per-Attempt ═══════════ */

static int t_artifact_attempt(void) {
    static artifact_store_t as;
    artifact_store_init(&as, "/tmp/nc_test_art_attempt");

    int w = artifact_write_attempt(&as, "n1", "output.txt", 1, "first try");
    ASSERT(w > 0);

    w = artifact_write_attempt(&as, "n1", "output.txt", 2, "second try");
    ASSERT(w > 0);

    char *c1 = artifact_read(&as, "n1", "output-attempt-1.txt");
    ASSERT_NOT_NULL(c1);
    ASSERT_STR_EQ(c1, "first try");
    free(c1);

    char *c2 = artifact_read(&as, "n1", "output-attempt-2.txt");
    ASSERT_NOT_NULL(c2);
    ASSERT_STR_EQ(c2, "second try");
    free(c2);

    artifact_append_attempt(&as, "n1", "stderr.log", 1, "line A\n");
    artifact_append_attempt(&as, "n1", "stderr.log", 1, "line B\n");
    char *log = artifact_read(&as, "n1", "stderr-attempt-1.log");
    ASSERT_NOT_NULL(log);
    ASSERT(strstr(log, "line A") != NULL);
    ASSERT(strstr(log, "line B") != NULL);
    free(log);

    artifact_store_free(&as);
    return 0;
}

/* ═══════════ Prompt Context Filtered Scope ═══════════ */

static int t_pctx_filtered(void) {
    static pipeline_t p;
    pipeline_init(&p, "filt-ctx", 4);
    static fanout_spec_t f;
    fanout_init(&f, "grp", "task");
    fanout_set_count(&f, 3);
    fanout_expand(&f, &p);
    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "r1", 0);
    pipeline_mark_running(&p, 1);
    pipeline_mark_failed(&p, 1, "err", 1);
    pipeline_mark_running(&p, 2);
    pipeline_mark_done(&p, 2, "r2", 0);

    scope_aggregate_t sa;
    scope_agg_init(&sa, "grp");
    scope_agg_collect(&sa, &p, &f);

    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    prompt_ctx_populate_filtered(&pc, "ok", &sa, PIPE_DONE);

    ASSERT_STR_EQ(prompt_ctx_get(&pc, "ok.total"), "2");
    ASSERT_STR_EQ(prompt_ctx_get(&pc, "ok.completed"), "2");

    prompt_ctx_populate_filtered(&pc, "err", &sa, PIPE_FAILED);
    ASSERT_STR_EQ(prompt_ctx_get(&pc, "err.total"), "1");
    ASSERT_STR_EQ(prompt_ctx_get(&pc, "err.failed"), "1");

    char *r = prompt_ctx_render(&pc, "Pass: {{ ok.total }}, Fail: {{ err.total }}");
    ASSERT(strstr(r, "Pass: 2") != NULL);
    ASSERT(strstr(r, "Fail: 1") != NULL);
    free(r);

    prompt_ctx_free(&pc);
    scope_agg_free(&sa);
    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Run Persist Cycle Restore ═══════════ */

static int t_persist_cycles(void) {
    static pipeline_t p;
    pipeline_init(&p, "cycle-persist", 2);
    p.max_iterations = 5;
    pipeline_add_node(&p, "code", "write");
    pipeline_add_node(&p, "test", "verify");
    pipeline_add_dep(&p, "test", "code");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    const char *targets[] = {"code"};
    pipe_orch_add_cycle(&o, "test", targets, 1);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    pipe_orch_complete_node(&o, "code", "v1", 0);
    pipe_orch_step(&o, ready, 8);

    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "PASS", true);
    pipe_orch_add_criteria(&o, "test", &rs);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "test", "nope", 0), 1);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_PENDING);
    ASSERT_INT_EQ(p.nodes[1].status, PIPE_PENDING);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "\"targets\": [\"code\"]") != NULL);

    static pipeline_t p2;
    pipeline_init(&p2, "cycle-persist", 2);
    pipeline_add_node(&p2, "code", "write");
    pipeline_add_node(&p2, "test", "verify");
    pipeline_add_dep(&p2, "test", "code");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);

    int rc = run_persist_restore_json(&o2, &p2, json);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(rcycle_iteration(&o2.cycles, "test"), rcycle_iteration(&o.cycles, "test"));
    ASSERT_INT_EQ(o2.cycles.nedges, 1);
    ASSERT_STR_EQ(o2.cycles.edges[0].tail_id, "test");
    ASSERT_INT_EQ(o2.cycles.edges[0].ntargets, 1);
    ASSERT_STR_EQ(o2.cycles.edges[0].targets[0], "code");

    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 1);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_RUNNING);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o2, "code", "v2", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 1);
    ASSERT_INT_EQ(p2.nodes[1].status, PIPE_RUNNING);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o2, "test", "PASS", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o2));
    ASSERT_INT_EQ(o2.state, ORCH_DONE);

    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

/* ═══════════ Pad Auto-Extract from Output ═══════════ */

static int t_orch_pad_auto(void) {
    static pipeline_t p;
    pipeline_init(&p, "pad-auto", 2);
    pipeline_add_node(&p, "worker", "pw");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    pipe_orch_complete_node(
        &o, "worker", "doing work\nPAD:critical bug in auth.c\nmore output\nPAD:fix needed", 0);

    char *text = pipe_orch_pad_read(&o);
    ASSERT_NOT_NULL(text);
    ASSERT(strstr(text, "critical bug in auth.c") != NULL);
    ASSERT(strstr(text, "fix needed") != NULL);
    free(text);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Render Prompt with Pad Suffix ═══════════ */

static int t_orch_render_pad(void) {
    static pipeline_t p;
    pipeline_init(&p, "render-pad", 2);
    pipeline_add_node(&p, "a", "do {{ a.output }}");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_enable_ctx_rendering(&o);

    pipe_orch_pad_write(&o, "scout", "found issue in db.c");

    char *rendered = pipe_orch_render_prompt(&o, "a");
    ASSERT_NOT_NULL(rendered);
    ASSERT(strstr(rendered, "found issue in db.c") != NULL);
    free(rendered);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Persist Cancel/Rerun Queues ═══════════ */

static int t_persist_queues(void) {
    static pipeline_t p;
    pipeline_init(&p, "queue-persist", 4);
    pipeline_add_node(&p, "x", "px");
    pipeline_add_node(&p, "y", "py");
    pipeline_add_node(&p, "z", "pz");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    pipe_orch_flag_node_cancel(&o, "x");
    pipe_orch_queue_rerun(&o, "y");
    pipe_orch_queue_rerun(&o, "z");
    ASSERT_INT_EQ(o.ncancel_flags, 1);
    ASSERT_INT_EQ(o.nrerun_queued, 2);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "cancel_flags") != NULL);
    ASSERT(strstr(json, "rerun_queue") != NULL);
    ASSERT(strstr(json, "\"x\"") != NULL);
    ASSERT(strstr(json, "\"y\"") != NULL);

    static pipeline_t p2;
    pipeline_init(&p2, "queue-persist", 4);
    pipeline_add_node(&p2, "x", "px");
    pipeline_add_node(&p2, "y", "py");
    pipeline_add_node(&p2, "z", "pz");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);

    int rc = run_persist_restore_json(&o2, &p2, json);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(o2.ncancel_flags, 1);
    ASSERT(pipe_orch_node_cancel_pending(&o2, "x"));
    ASSERT_INT_EQ(o2.nrerun_queued, 2);

    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_run_cancel(void) {
    static pipeline_t p;
    pipeline_init(&p, "cancel-persist", 2);
    pipeline_add_node(&p, "a", "pa");
    pipeline_add_node(&p, "b", "pb");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);
    pipe_orch_enable_ctx_rendering(&o);
    pipe_orch_cancel(&o);
    nc_strlcpy(o.run_scope.token, "cancel-run-token", sizeof(o.run_scope.token));

    ASSERT_INT_EQ(o.state, ORCH_RUNNING);
    ASSERT_TRUE(cscope_should_stop(&o.run_scope));

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "\"run_scope_state\"") != NULL);
    ASSERT(strstr(json, "\"run_scope_token\"") != NULL);
    ASSERT(strstr(json, "cancel-run-token") != NULL);

    static pipeline_t p2;
    pipeline_init(&p2, "cancel-persist", 2);
    pipeline_add_node(&p2, "a", "pa");
    pipeline_add_node(&p2, "b", "pb");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);
    ASSERT_INT_EQ(run_persist_restore_json(&o2, &p2, json), 0);
    ASSERT_TRUE(o2.ctx_rendering);
    ASSERT_TRUE(cscope_should_stop(&o2.run_scope));
    ASSERT_INT_EQ(o2.state, ORCH_RUNNING);
    ASSERT_STR_EQ(o2.run_scope.token, "cancel-run-token");

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 0);
    ASSERT_INT_EQ(o2.state, ORCH_CANCELLED);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_CANCELLED);
    ASSERT_INT_EQ(p2.nodes[1].status, PIPE_CANCELLED);
    ASSERT_TRUE(pipe_orch_is_terminal(&o2));

    const char *token_line = "    \"run_scope_token\": \"cancel-run-token\",\n";
    char *legacy_json = nc_xstrdup(json);
    ASSERT_NOT_NULL(legacy_json);
    char *token_pos = strstr(legacy_json, token_line);
    ASSERT_NOT_NULL(token_pos);
    memmove(token_pos, token_pos + strlen(token_line), strlen(token_pos + strlen(token_line)) + 1);

    static pipeline_t p3;
    pipeline_init(&p3, "cancel-persist", 2);
    pipeline_add_node(&p3, "a", "pa");
    pipeline_add_node(&p3, "b", "pb");

    static pipe_orch_t o3;
    pipe_orch_init(&o3, &p3);
    ASSERT_INT_EQ(run_persist_restore_json(&o3, &p3, legacy_json), 0);
    ASSERT_STR_EQ(o3.run_scope.token, p3.name);

    free(json);
    free(legacy_json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipe_orch_free(&o3);
    pipeline_free(&p);
    pipeline_free(&p2);
    pipeline_free(&p3);
    return 0;
}

static int t_persist_inflight_restart(void) {
    static pipeline_t p;
    pipeline_init(&p, "inflight-restart", 1);
    pipeline_add_node(&p, "a", "pa");
    pipeline_add_node(&p, "b", "pb");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_RUNNING);
    ASSERT_INT_EQ(p.nodes[0].attempt, 1);
    ASSERT_INT_EQ(p.nodes[1].status, PIPE_PENDING);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);

    static pipeline_t p2;
    pipeline_init(&p2, "inflight-restart", 1);
    pipeline_add_node(&p2, "a", "pa");
    pipeline_add_node(&p2, "b", "pb");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);
    ASSERT_INT_EQ(run_persist_restore_json(&o2, &p2, json), 0);

    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_PENDING);
    ASSERT_INT_EQ(p2.nodes[0].started_ms, 0);
    ASSERT_INT_EQ(p2.nodes[0].finished_ms, 0);
    ASSERT(p2.nodes[0].output == NULL);
    ASSERT_INT_EQ(p2.nodes[0].attempt, 1);

    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 1);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_RUNNING);
    ASSERT_INT_EQ(p2.nodes[0].attempt, 2);

    ASSERT_INT_EQ(pipe_orch_complete_node(&o2, "a", "ok-a", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 1);
    ASSERT_INT_EQ(p2.nodes[1].status, PIPE_RUNNING);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o2, "b", "ok-b", 0), 0);
    ASSERT_INT_EQ(pipe_orch_step(&o2, ready, 8), 0);
    ASSERT_TRUE(pipe_orch_is_terminal(&o2));
    ASSERT_INT_EQ(o2.state, ORCH_DONE);

    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_artifact_rebuild(void) {
    char artdir[] = "/tmp/nevercode-persist-artifacts-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(artdir));

    static pipeline_t p;
    pipeline_init(&p, "persist-artifacts", 1);
    pipeline_add_node(&p, "solo", "run");

    static artifact_store_t as;
    artifact_store_init(&as, artdir);

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_attach_artifacts(&o, &as);
    pipe_orch_begin(&o);

    int ready[8];
    ASSERT_INT_EQ(pipe_orch_step(&o, ready, 8), 1);
    ASSERT_INT_EQ(pipe_orch_complete_node(&o, "solo", "artifact payload", 0), 0);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);

    static pipeline_t p2;
    pipeline_init(&p2, "persist-artifacts", 1);
    pipeline_add_node(&p2, "solo", "run");

    static artifact_store_t as2;
    artifact_store_init(&as2, artdir);

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);
    pipe_orch_attach_artifacts(&o2, &as2);
    ASSERT_INT_EQ(run_persist_restore_json(&o2, &p2, json), 0);

    ASSERT(artifact_slot_count(&as2, "solo") >= 2);
    char *content = artifact_read(&as2, "solo", "output.txt");
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "artifact payload");
    free(content);

    char *manifest = artifact_format_manifest(&as2, "solo");
    ASSERT_NOT_NULL(manifest);
    ASSERT(strstr(manifest, "output.txt") != NULL);
    ASSERT(strstr(manifest, "attempt") != NULL);
    free(manifest);

    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    artifact_store_free(&as);
    artifact_store_free(&as2);
    pipeline_free(&p);
    pipeline_free(&p2);
    ASSERT_INT_EQ(th_remove_tree(artdir), 0);
    return 0;
}

static int t_persist_probe_criteria(void) {
    static pipeline_t p;
    pipeline_init(&p, "probe-criteria", 2);
    pipeline_add_node(&p, "check", "verify");
    pipeline_add_node(&p, "w_0", "worker");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);

    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "PASS", true);
    sc_add_exit_zero(&rs);
    ASSERT_INT_EQ(pipe_orch_add_criteria(&o, "check", &rs), 0);

    ASSERT_INT_EQ(pipe_orch_add_probe(&o, "w", 7), 0);
    probe_record_tick(&o.probes[0]);
    long probe_now = nc_mono_ms();
    o.probes[0].last_tick_ms = probe_now - 25;
    o.probes[0].next_tick_ms = probe_now + 60;
    ASSERT_INT_EQ(probe_add_action(&o.probes[0], PROBE_ACT_RESTART, "w_0", "need retry"), 0);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "\"criteria\"") != NULL);
    ASSERT(strstr(json, "\"probes\"") != NULL);

    static pipeline_t p2;
    pipeline_init(&p2, "probe-criteria", 2);
    pipeline_add_node(&p2, "check", "verify");
    pipeline_add_node(&p2, "w_0", "worker");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);

    ASSERT_INT_EQ(run_persist_restore_json(&o2, &p2, json), 0);
    ASSERT_INT_EQ(o2.ncriteria, 1);
    ASSERT_INT_EQ(o2.criteria_map[0].rules.nrules, 2);
    ASSERT_INT_EQ(o2.nprobes, 1);
    ASSERT_STR_EQ(o2.probes[0].watch_group, "w");
    ASSERT_INT_EQ(o2.probes[0].interval_s, 7);
    ASSERT_INT_EQ(o2.probes[0].tick_count, 1);
    ASSERT_INT_EQ(o2.probes[0].state, PROBE_WATCHING);
    ASSERT_INT_EQ(o2.probes[0].nactions, 1);
    ASSERT_INT_EQ(o2.probes[0].actions[0].kind, PROBE_ACT_RESTART);
    ASSERT_STR_EQ(o2.probes[0].actions[0].target_id, "w_0");
    ASSERT_STR_EQ(o2.probes[0].actions[0].reason, "need retry");
    ASSERT_FALSE(probe_should_tick(&o2.probes[0], nc_mono_ms()));
    usleep(80000);
    ASSERT_TRUE(probe_should_tick(&o2.probes[0], nc_mono_ms()));

    p2.nodes[1].status = PIPE_FAILED;
    ASSERT_INT_EQ(probe_apply_actions(&o2.probes[0], &p2), 1);
    ASSERT_INT_EQ(p2.nodes[1].status, PIPE_PENDING);

    ASSERT_INT_EQ(pipe_orch_complete_node(&o2, "check", "PASS", 1), -1);
    ASSERT_INT_EQ(p2.nodes[0].status, PIPE_FAILED);

    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    return 0;
}

static int t_persist_runtime_supervision(void) {
    char repo_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-runtime-persist-XXXXXX";

    ASSERT_NOT_NULL(getcwd(repo_cwd, sizeof(repo_cwd)));
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    static pipeline_t p;
    pipeline_init(&p, "runtime-persist", 1);
    pipeline_add_node(&p, "solo", "run");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);

    watchdog_init(20);
    watchdog_begin_turn(1);
    usleep(30000);
    (void)watchdog_check();
    watchdog_end_turn(false);

    failure_router_init();
    for (int i = 0; i < 4; i++)
        harness_classify_failure("rate_limit", 429);

    stall_detector_init();
    for (int i = 0; i < STALL_MIN_REPEAT; i++)
        stall_detector_record_turn("Read", 10, false);
    for (int i = 0; i < STALL_READ_LOOP_THRESHOLD; i++)
        stall_detector_record_read("/tmp/runtime-loop.txt");

    context_fatigue_init(100000);
    context_fatigue_record(1, 50000);
    context_fatigue_record(2, 86000);

    output_eval_init();
    output_eval_check_ex("Done.\n```c\nint main(void){return 0;}\n```", "write a main", 1);
    for (int i = 0; i < OEVAL_DEGRADE_THRESHOLD; i++)
        output_eval_check_ex("", "g", 0);

    auto_test_init(repo_cwd);
    char *auto_cmd = auto_test_build_command("src/core/main.c");
    ASSERT_NOT_NULL(auto_cmd);
    free(auto_cmd);
    auto_test_record_run(2, true);
    auto_test_set_enabled(false);

    checkpoint_discipline_init("persist", 2);
    ASSERT_FALSE(checkpoint_discipline_should_fire(1, true));

    tool_verify_init();
    tool_verify_check("glob", NULL, 0, 0, false);
    tool_verify_check("glob", NULL, 0, 0, false);

    doom_loop_init();
    for (int i = 0; i < DOOM_LOOP_THRESHOLD - 1; i++)
        ASSERT_FALSE(doom_loop_record("read", "{\"path\":\"same.c\"}"));

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "\"watchdog\"") != NULL);
    ASSERT(strstr(json, "\"failure_router\"") != NULL);
    ASSERT(strstr(json, "\"stall_detector\"") != NULL);
    ASSERT(strstr(json, "\"context_fatigue\"") != NULL);
    ASSERT(strstr(json, "\"output_eval\"") != NULL);
    ASSERT(strstr(json, "\"auto_test\"") != NULL);
    ASSERT(strstr(json, "\"checkpoint_discipline\"") != NULL);
    ASSERT(strstr(json, "\"tool_verify\"") != NULL);
    ASSERT(strstr(json, "\"doom_loop\"") != NULL);

    watchdog_init(5000);
    failure_router_init();
    stall_detector_init();
    context_fatigue_init(4096);
    output_eval_init();
    auto_test_init("/tmp");
    checkpoint_discipline_init("reset", 10);
    tool_verify_init();
    doom_loop_init();

    static pipeline_t p2;
    pipeline_init(&p2, "runtime-persist", 1);
    pipeline_add_node(&p2, "solo", "run");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);

    ASSERT_INT_EQ(run_persist_restore_json(&o2, &p2, json), 0);
    ASSERT_INT_EQ(watchdog_timeout_count(), 1);
    ASSERT_INT_EQ(watchdog_consecutive_timeouts(), 1);
    ASSERT(watchdog_longest_turn_ms() >= 20);
    ASSERT_INT_EQ(harness_failure_window_count(FAIL_TRANSIENT), 4);
    ASSERT(harness_adaptive_scale(FAIL_TRANSIENT) > 1.0);
    ASSERT_INT_EQ(stall_detector_window_count(), STALL_MIN_REPEAT);
    ASSERT(stall_detector_read_loop());
    ASSERT_INT_EQ(stall_detector_check(), STALL_LOOP_DETECTED);
    ASSERT_INT_EQ(context_fatigue_pct(), 86);
    ASSERT_TRUE(context_fatigue_needs_compact());
    ASSERT_TRUE(context_fatigue_velocity() > 0.0);
    ASSERT_INT_EQ(output_eval_total(), OEVAL_DEGRADE_THRESHOLD + 1);
    ASSERT_INT_EQ(output_eval_pass_count(), 1);
    ASSERT_INT_EQ(output_eval_consecutive_weak(), OEVAL_DEGRADE_THRESHOLD);
    ASSERT_TRUE(output_eval_degrading());
    ASSERT_FALSE(auto_test_is_enabled());
    ASSERT_INT_EQ(auto_test_run_count(), 1);
    ASSERT_INT_EQ(auto_test_pass_count(), 1);
    auto_test_set_enabled(true);
    ASSERT_FALSE(auto_test_should_run(4));
    ASSERT_TRUE(auto_test_should_run(5));
    auto_cmd = auto_test_build_command("src/core/main.c");
    ASSERT_NOT_NULL(auto_cmd);
    ASSERT_STR_EQ(auto_cmd, "make test-unit");
    free(auto_cmd);
    ASSERT_TRUE(checkpoint_discipline_should_fire(2, true));
    ASSERT_INT_EQ(tool_verify_total_count(), 2);
    ASSERT_INT_EQ(tool_verify_flagged_count(), 2);
    ASSERT_INT_EQ(tool_verify_check("glob", NULL, 0, 0, false), TV_PATTERN_ALERT);
    ASSERT_TRUE(doom_loop_record("read", "{\"path\":\"same.c\"}"));
    ASSERT_INT_EQ(doom_loop_fire_count(), 1);
    ASSERT_STR_EQ(doom_loop_last_tool(), "read");

    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    watchdog_free();
    failure_router_free();
    stall_detector_free();
    context_fatigue_free();
    output_eval_free();
    auto_test_free();
    checkpoint_discipline_free();
    tool_verify_free();
    doom_loop_reset();
    ASSERT_INT_EQ(chdir(repo_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

/* ═══════════ Rerun Queue Prevents Premature Terminal ═══════════ */

static int t_orch_rerun_holds_terminal(void) {
    static pipeline_t p;
    pipeline_init(&p, "hold-term", 2);
    pipeline_add_node(&p, "m", "pm");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    pipe_orch_complete_node(&o, "m", "ok", 0);

    pipe_orch_queue_rerun(&o, "m");
    pipe_orch_step(&o, ready, 8);
    ASSERT(!pipe_orch_is_terminal(&o));

    pipe_orch_complete_node(&o, "m", "ok2", 0);
    pipe_orch_step(&o, ready, 8);
    ASSERT(pipe_orch_is_terminal(&o));

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Per-Attempt Artifact from Orchestrator ═══════════ */

static int t_orch_attempt_artifacts(void) {
    static pipeline_t p;
    pipeline_init(&p, "attempt-art", 2);
    pipeline_add_node(&p, "n", "pn");

    artifact_store_t *as = calloc(1, sizeof(*as));
    ASSERT_NOT_NULL(as);
    artifact_store_init(as, "/tmp/nc_test_orch_attempt");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_attach_artifacts(&o, as);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    int rc = pipe_orch_complete_node(&o, "n", "first output", 0);
    ASSERT_INT_EQ(rc, 0);

    char *base = artifact_read(as, "n", "output.txt");
    ASSERT_NOT_NULL(base);
    ASSERT_STR_EQ(base, "first output");
    free(base);

    /* Verify per-attempt write works directly on the same store */
    int wr = artifact_write_attempt(as, "n", "check.txt", 1, "direct");
    ASSERT(wr > 0);
    char *d = artifact_read(as, "n", "check-attempt-1.txt");
    ASSERT_NOT_NULL(d);
    ASSERT_STR_EQ(d, "direct");
    free(d);

    char *manifest = artifact_format_manifest(as, "n");
    ASSERT_NOT_NULL(manifest);
    /* The orchestrator should have written output.txt and output-attempt-1.txt */
    ASSERT(strstr(manifest, "output.txt") != NULL);
    ASSERT(strstr(manifest, "attempt") != NULL);
    free(manifest);

    pipe_orch_free(&o);
    artifact_store_free(as);
    free(as);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Cancel Flag Cleanup After Application ═══════════ */

static int t_orch_cancel_flag_cleanup(void) {
    static pipeline_t p;
    pipeline_init(&p, "cleanup", 4);
    pipeline_add_node(&p, "a", "pa");
    pipeline_add_node(&p, "b", "pb");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_RUNNING);

    pipe_orch_flag_node_cancel(&o, "a");
    pipe_orch_flag_node_cancel(&o, "b");
    ASSERT_INT_EQ(o.ncancel_flags, 2);

    pipe_orch_step(&o, ready, 8);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_CANCELLED);
    ASSERT(o.ncancel_flags < 2);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Probe Routes Through Queues Without Double-Apply ═══════════ */

static int t_orch_probe_routes_queues(void) {
    static pipeline_t p;
    pipeline_init(&p, "probe-route", 4);
    pipeline_add_node(&p, "w_0", "pw");
    pipeline_add_node(&p, "w_1", "pw");
    pipeline_mark_running(&p, 0);
    pipeline_mark_running(&p, 1);

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    pipe_orch_add_probe(&o, "w", 1);
    o.probes[0].next_tick_ms = 0;
    probe_add_action(&o.probes[0], PROBE_ACT_RESTART, "w_0", "need retry");

    int ready[8];
    pipe_orch_step(&o, ready, 8);

    ASSERT_INT_EQ(o.probes[0].nactions, 0);
    ASSERT_INT_EQ(o.nrerun_queued, 1);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Orchestrator Step Integrates Reruns ═══════════ */

static int t_orch_step_rerun(void) {
    static pipeline_t p;
    pipeline_init(&p, "step-rerun", 4);
    pipeline_add_node(&p, "a", "pa");
    pipeline_add_node(&p, "b", "pb");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    pipe_orch_step(&o, ready, 8);
    pipe_orch_complete_node(&o, "a", "ok", 0);
    pipe_orch_complete_node(&o, "b", "ok", 0);
    pipe_orch_step(&o, ready, 8);
    ASSERT(pipe_orch_is_terminal(&o));

    o.state = ORCH_RUNNING;
    pipe_orch_queue_rerun(&o, "b");

    int nr = pipe_orch_step(&o, ready, 8);
    ASSERT(p.nodes[1].status == PIPE_RUNNING || p.nodes[1].status == PIPE_PENDING);
    ASSERT(nr >= 1);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

/* ═══════════ Test Suite Entry ═══════════ */

int test_pipeline_system_all(void) {
    TEST_SUITE("pipeline_system");
    TEST_COUNTERS;

    /* Pipeline DAG */
    RUN_TEST(t_pipe_basic);
    RUN_TEST(t_pipe_duplicate_node);
    RUN_TEST(t_pipe_cycle_detection);
    RUN_TEST(t_pipe_skip_downstream);
    RUN_TEST(t_pipe_parallel);
    RUN_TEST(t_pipe_cancel);
    RUN_TEST(t_pipe_status_names);

    /* Fanout / Merge */
    RUN_TEST(t_fanout_count);
    RUN_TEST(t_fanout_values);
    RUN_TEST(t_merge_batch);

    /* Success Criteria */
    RUN_TEST(t_sc_output_contains);
    RUN_TEST(t_sc_case_insensitive);
    RUN_TEST(t_sc_exit_zero);
    RUN_TEST(t_sc_conjunction);
    RUN_TEST(t_sc_not_contains);
    RUN_TEST(t_sc_file_exists);
    RUN_TEST(t_sc_kind_names);

    /* Cancel Scope */
    RUN_TEST(t_cscope_basic);
    RUN_TEST(t_cscope_hierarchy);
    RUN_TEST(t_cscope_child_only);

    /* Shared Pad */
    RUN_TEST(t_spad_basic);
    RUN_TEST(t_spad_dedup);
    RUN_TEST(t_spad_persist);
    RUN_TEST(t_spad_load_replaces_state);
    RUN_TEST(t_spad_persist_run_token_metadata);

    /* Event Ledger */
    RUN_TEST(t_ledger_basic);
    RUN_TEST(t_ledger_persist);
    RUN_TEST(t_ledger_since);

    /* Preflight Report */
    RUN_TEST(t_pflt_basic);
    RUN_TEST(t_pflt_warnings);

    /* Periodic Probe */
    RUN_TEST(t_probe_basic);
    RUN_TEST(t_probe_settled);
    RUN_TEST(t_probe_snapshot_restore_timing);

    /* Retry Cycle */
    RUN_TEST(t_rcycle_basic);

    /* Orchestrator */
    RUN_TEST(t_orch_full_run);
    RUN_TEST(t_orch_with_criteria);
    RUN_TEST(t_orch_with_cycle);
    RUN_TEST(t_orch_cancel);
    RUN_TEST(t_orch_pad);

    /* Scope Aggregate */
    RUN_TEST(t_scope_collect);
    RUN_TEST(t_scope_empty);

    /* Prompt Context Rendering */
    RUN_TEST(t_pctx_render);
    RUN_TEST(t_pctx_pipeline);
    RUN_TEST(t_pctx_scope);

    /* Node Artifact Storage */
    RUN_TEST(t_artifact_basic);

    /* Ledger Watcher */
    RUN_TEST(t_watcher_basic);
    RUN_TEST(t_watcher_overflow);

    /* Run Persistence */
    RUN_TEST(t_persist_snapshot);
    RUN_TEST(t_persist_file);
    RUN_TEST(t_persist_file_ledger);
    RUN_TEST(t_persist_begin_clears_stale_ledger_sidecars);
    RUN_TEST(t_persist_json_ledger);
    RUN_TEST(t_persist_json_ledger_prefers_fresher_snapshot);
    RUN_TEST(t_persist_json_ignores_mismatched_events_inside_snapshot);
    RUN_TEST(t_persist_save_reports_companion_failure);
    RUN_TEST(t_persist_file_pad_merges_fresher_disk);
    RUN_TEST(t_persist_file_pad_ignores_mismatched_run_identity);
    RUN_TEST(t_persist_file_prefers_fresher_sidecar_snapshot);
    RUN_TEST(t_persist_file_legacy_snapshot_ignores_tokened_sidecar);
    RUN_TEST(t_persist_file_ignores_mismatched_run_identity);
    RUN_TEST(t_persist_file_replays_fresher_log_state);
    RUN_TEST(t_persist_file_ignores_tokenless_fresher_log);
    RUN_TEST(t_persist_file_ignores_mismatched_events_inside_log);
    RUN_TEST(t_persist_file_ignores_tokenless_events_inside_tokened_log);
    RUN_TEST(t_persist_file_replays_fail_fast_skip);
    RUN_TEST(t_persist_scoped);
    RUN_TEST(t_persist_outputs_and_pad);

    /* Scope Prefix Collect */
    RUN_TEST(t_scope_prefix);

    /* Prompt Context Edge Cases */
    RUN_TEST(t_pctx_edge);

    /* Orchestrator Integration */
    RUN_TEST(t_orch_artifacts);
    RUN_TEST(t_orch_render);

    /* Watcher Idempotence */
    RUN_TEST(t_watcher_idempotent);

    /* Node-Level Cancel/Rerun */
    RUN_TEST(t_orch_node_cancel_flag);
    RUN_TEST(t_orch_rerun_queue);
    RUN_TEST(t_orch_rerun_defers_running);
    RUN_TEST(t_orch_step_cancel_flag);
    RUN_TEST(t_orch_step_cancel_pending);
    RUN_TEST(t_orch_step_rerun);

    /* Scope Subset / Partition */
    RUN_TEST(t_scope_subset);
    RUN_TEST(t_scope_partition);

    /* Per-Attempt Artifacts */
    RUN_TEST(t_artifact_attempt);

    /* Filtered Prompt Context */
    RUN_TEST(t_pctx_filtered);

    /* Cycle Persistence Restore */
    RUN_TEST(t_persist_cycles);

    /* Pad Auto-Extract */
    RUN_TEST(t_orch_pad_auto);

    /* Render with Pad Suffix */
    RUN_TEST(t_orch_render_pad);

    /* Persist Cancel/Rerun Queues */
    RUN_TEST(t_persist_queues);
    RUN_TEST(t_persist_run_cancel);
    RUN_TEST(t_persist_inflight_restart);
    RUN_TEST(t_persist_artifact_rebuild);
    RUN_TEST(t_persist_probe_criteria);
    RUN_TEST(t_persist_runtime_supervision);

    /* Rerun Holds Terminal */
    RUN_TEST(t_orch_rerun_holds_terminal);

    /* Per-Attempt Artifacts from Orchestrator */
    RUN_TEST(t_orch_attempt_artifacts);

    /* Cancel Flag Cleanup */
    RUN_TEST(t_orch_cancel_flag_cleanup);

    /* Probe Routes Through Queues */
    RUN_TEST(t_orch_probe_routes_queues);

    TEST_RETURN_FAILED();
}
