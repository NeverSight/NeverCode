/*
 * pipe_orchestrator.c — Unified pipeline run-loop tying all modules together.
 *
 * Drives a pipeline through its lifecycle: validates, emits events,
 * checks cancel scopes, evaluates success criteria, applies retry
 * cycles, ticks periodic probes, and merges shared pad entries.
 * Each call to pipe_orch_step() advances one scheduling cycle.
 */

#include "harness.h"

static void _orch_assign_run_token(pipe_orch_t *o) {
    struct timespec ts;
    if (!o || !o->pipeline)
        return;
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(o->run_scope.token, sizeof(o->run_scope.token), "%.20s-%ld-%09ld-%d",
             o->pipeline->name, (long)ts.tv_sec, (long)ts.tv_nsec, (int)getpid());
}

void pipe_orch_init(pipe_orch_t *o, pipeline_t *p) {
    memset(o, 0, sizeof(*o));
    o->pipeline = p;
    cscope_init(&o->run_scope, p->name, NULL);
    ledger_init(&o->ledger);
    rcycle_init(&o->cycles, p->max_iterations);
    shared_pad_init(&o->pad);
    o->state = ORCH_IDLE;
}

void pipe_orch_init_persistent(pipe_orch_t *o, pipeline_t *p, const char *log_path,
                               const char *snap_path, const char *pad_path) {
    pipe_orch_init(o, p);
    if (log_path || snap_path)
        ledger_init_persistent(&o->ledger, log_path, snap_path);
    if (pad_path)
        shared_pad_init_persistent(&o->pad, pad_path);
}

void pipe_orch_free(pipe_orch_t *o) {
    ledger_free(&o->ledger);
    rcycle_free(&o->cycles);
    shared_pad_free(&o->pad);
    for (int i = 0; i < o->nprobes; i++)
        probe_free(&o->probes[i]);
    memset(o, 0, sizeof(*o));
}

int pipe_orch_add_criteria(pipe_orch_t *o, const char *node_id, const sc_ruleset_t *rs) {
    if (!node_id || !rs || o->ncriteria >= PIPE_MAX_NODES)
        return -1;
    int idx = pipeline_find_node(o->pipeline, node_id);
    if (idx < 0)
        return -1;
    nc_strlcpy(o->criteria_map[o->ncriteria].node_id, node_id, sizeof(o->criteria_map[0].node_id));
    o->criteria_map[o->ncriteria].rules = *rs;
    o->ncriteria++;
    return 0;
}

int pipe_orch_add_probe(pipe_orch_t *o, const char *group, int interval_s) {
    if (o->nprobes >= PIPE_ORCH_MAX_PROBES)
        return -1;
    probe_init(&o->probes[o->nprobes], group, interval_s);
    o->nprobes++;
    return 0;
}

int pipe_orch_add_cycle(pipe_orch_t *o, const char *tail, const char **targets, int ntargets) {
    return rcycle_add_edge(&o->cycles, tail, targets, ntargets);
}

static const sc_ruleset_t *_find_criteria(const pipe_orch_t *o, const char *node_id) {
    for (int i = 0; i < o->ncriteria; i++)
        if (strcmp(o->criteria_map[i].node_id, node_id) == 0)
            return &o->criteria_map[i].rules;
    return NULL;
}

int pipe_orch_begin(pipe_orch_t *o) {
    char err[NC_MSG_MAX];
    if (!pipeline_validate(o->pipeline, err, sizeof(err))) {
        ledger_append(&o->ledger, "validation_failed", NULL, err);
        return -1;
    }
    _orch_assign_run_token(o);
    ledger_set_run_token(&o->ledger, o->run_scope.token);
    shared_pad_set_run_token(&o->pad, o->run_scope.token);
    ledger_prepare_new_run(&o->ledger);
    o->state = ORCH_RUNNING;
    o->started_ms = nc_mono_ms();
    ledger_append(&o->ledger, "run_started", NULL, o->pipeline->name);
    return 0;
}

int pipe_orch_step(pipe_orch_t *o, int *ready, int max_ready) {
    if (o->state != ORCH_RUNNING)
        return 0;

    pipeline_t *p = o->pipeline;

    if (cscope_should_stop(&o->run_scope)) {
        for (int i = 0; i < p->nnodes; i++) {
            if (p->nodes[i].status == PIPE_PENDING || p->nodes[i].status == PIPE_QUEUED) {
                pipeline_mark_cancelled(p, i);
                ledger_append(&o->ledger, "node_cancelled", p->nodes[i].id, "run_scope_cancelled");
            }
        }
        o->state = ORCH_CANCELLED;
        ledger_append(&o->ledger, "run_cancelled", NULL, NULL);
        return 0;
    }

    if (p->fail_fast) {
        for (int i = 0; i < p->nnodes; i++) {
            if (p->nodes[i].status == PIPE_FAILED) {
                int sk = pipeline_skip_downstream(p, i);
                if (sk > 0) {
                    char d[NC_FMT_MAX];
                    snprintf(d, sizeof(d), "skipped %d downstream", sk);
                    ledger_append(&o->ledger, "fail_fast_skip", p->nodes[i].id, d);
                }
            }
        }
    }

    /* Skip nodes whose deps have failed (non fail_fast paths) */
    for (int i = 0; i < p->nnodes; i++) {
        if (p->nodes[i].status != PIPE_PENDING)
            continue;
        bool blocked = false;
        for (int d = 0; d < p->nodes[i].ndeps; d++) {
            int di = pipeline_find_node(p, p->nodes[i].deps[d]);
            if (di >= 0) {
                pipe_status_t ds = p->nodes[di].status;
                if (ds == PIPE_FAILED || ds == PIPE_SKIPPED || ds == PIPE_CANCELLED) {
                    /* Check if this node is part of a retry cycle */
                    if (!rcycle_should_restart(&o->cycles, p->nodes[i].id)) {
                        blocked = true;
                        break;
                    }
                }
            }
        }
        if (blocked) {
            p->nodes[i].status = PIPE_SKIPPED;
            p->nodes[i].finished_ms = nc_mono_ms();
            ledger_append(&o->ledger, "node_skipped", p->nodes[i].id, "upstream_failure");
        }
    }

    /* Apply queued node-level cancels before scheduling, then purge acted flags */
    {
        int c = 0;
        while (c < o->ncancel_flags) {
            int ci = pipeline_find_node(p, o->cancel_flags[c]);
            if (ci >= 0 &&
                (p->nodes[ci].status == PIPE_RUNNING || p->nodes[ci].status == PIPE_PENDING ||
                 p->nodes[ci].status == PIPE_QUEUED)) {
                pipeline_mark_cancelled(p, ci);
                ledger_append(&o->ledger, "node_cancelled", o->cancel_flags[c], "flagged");
                for (int j = c; j < o->ncancel_flags - 1; j++)
                    nc_strlcpy(o->cancel_flags[j], o->cancel_flags[j + 1],
                               sizeof(o->cancel_flags[0]));
                o->ncancel_flags--;
            } else if (ci < 0 || p->nodes[ci].status == PIPE_CANCELLED ||
                       p->nodes[ci].status == PIPE_DONE || p->nodes[ci].status == PIPE_FAILED ||
                       p->nodes[ci].status == PIPE_SKIPPED) {
                for (int j = c; j < o->ncancel_flags - 1; j++)
                    nc_strlcpy(o->cancel_flags[j], o->cancel_flags[j + 1],
                               sizeof(o->cancel_flags[0]));
                o->ncancel_flags--;
            } else {
                c++;
            }
        }
    }

    /* Drain deferred reruns: reset terminal nodes back to pending */
    pipe_orch_drain_reruns(o);

    /* Tick probes — extract actions first, then record tick */
    long now = nc_mono_ms();
    for (int i = 0; i < o->nprobes; i++) {
        if (probe_should_tick(&o->probes[i], now)) {
            int routed = 0;
            int nact = o->probes[i].nactions;
            probe_action_t staged[PROBE_MAX_ACTIONS];
            if (nact > PROBE_MAX_ACTIONS)
                nact = PROBE_MAX_ACTIONS;
            for (int a = 0; a < nact; a++)
                staged[a] = o->probes[i].actions[a];

            probe_record_tick(&o->probes[i]);

            for (int a = 0; a < nact; a++) {
                if (staged[a].kind == PROBE_ACT_CANCEL) {
                    pipe_orch_flag_node_cancel(o, staged[a].target_id);
                    routed++;
                } else if (staged[a].kind == PROBE_ACT_RESTART) {
                    pipe_orch_queue_rerun(o, staged[a].target_id);
                    routed++;
                }
            }
            if (routed > 0) {
                char d[NC_FMT_MAX];
                snprintf(d, sizeof(d), "routed %d actions", routed);
                ledger_append(&o->ledger, "probe_acted", o->probes[i].watch_group, d);
            }
            if (probe_group_settled(&o->probes[i], p))
                probe_finalize(&o->probes[i]);
        }
    }

    /* Collect ready nodes */
    int nr = pipeline_ready_nodes(p, ready, max_ready);

    /* Respect concurrency: count running nodes */
    int running = 0;
    for (int i = 0; i < p->nnodes; i++)
        if (p->nodes[i].status == PIPE_RUNNING)
            running++;

    int slots = p->concurrency - running;
    if (slots < 0)
        slots = 0;
    if (nr > slots)
        nr = slots;

    for (int i = 0; i < nr; i++) {
        pipeline_mark_running(p, ready[i]);
        ledger_append(&o->ledger, "node_started", p->nodes[ready[i]].id, NULL);
    }

    if (pipeline_all_terminal(p) && o->nrerun_queued == 0) {
        bool any_failed = false;
        for (int i = 0; i < p->nnodes; i++)
            if (p->nodes[i].status == PIPE_FAILED)
                any_failed = true;
        o->state = any_failed ? ORCH_FAILED : ORCH_DONE;
        o->finished_ms = nc_mono_ms();
        p->status = any_failed ? PIPE_FAILED : PIPE_DONE;
        p->finished_ms = o->finished_ms;
        ledger_append(&o->ledger, "run_completed", NULL, any_failed ? "failed" : "done");
    }

    return nr;
}

int pipe_orch_complete_node(pipe_orch_t *o, const char *node_id, const char *output,
                            int exit_code) {
    pipeline_t *p = o->pipeline;
    int idx = pipeline_find_node(p, node_id);
    if (idx < 0)
        return -1;

    /* Evaluate success criteria if configured */
    const sc_ruleset_t *rs = _find_criteria(o, node_id);
    bool success = (exit_code == 0);
    char detail[NC_MSG_MAX] = {0};

    if (rs && rs->nrules > 0) {
        success = sc_evaluate(rs, output, exit_code, NULL, detail, sizeof(detail));
    }

    int attempt = p->nodes[idx].attempt + 1;

    if (o->artifacts && output) {
        artifact_write(o->artifacts, node_id, PIPE_ARTIFACT_OUTPUT, output);
        artifact_write_attempt(o->artifacts, node_id, PIPE_ARTIFACT_OUTPUT, attempt, output);
    }
    if (o->artifacts && detail[0])
        artifact_write_attempt(o->artifacts, node_id, PIPE_ARTIFACT_RESULT, attempt, detail);

    if (output) {
        const char *line = output;
        while (*line) {
            const char *eol = strchr(line, '\n');
            size_t llen = eol ? (size_t)(eol - line) : strlen(line);
            if (llen > 4 && nc_starts_with(line, "PAD:")) {
                char entry[SPAD_LINE_MAX];
                size_t elen = llen - 4;
                if (elen >= sizeof(entry))
                    elen = sizeof(entry) - 1;
                memcpy(entry, line + 4, elen);
                entry[elen] = '\0';
                shared_pad_merge(&o->pad, node_id, entry);
            }
            line += llen + (eol ? 1 : 0);
        }
    }

    if (success) {
        pipeline_mark_done(p, idx, output, exit_code);
        ledger_append_result(&o->ledger, "node_done", node_id, detail[0] ? detail : NULL, output,
                             exit_code);
        return 0;
    }

    if (rcycle_should_restart(&o->cycles, node_id)) {
        int iter = rcycle_do_restart(&o->cycles, node_id, p);
        char d[NC_TAG_MAX];
        snprintf(d, sizeof(d), "iteration %d: %s", iter, detail[0] ? detail : "criteria not met");
        ledger_append(&o->ledger, "node_cycle_restart", node_id, d);
        return 1;
    }

    pipeline_mark_failed(p, idx, output, exit_code);
    ledger_append_result(&o->ledger, "node_failed", node_id, detail[0] ? detail : NULL, output,
                         exit_code);

    if (p->fail_fast) {
        int sk = pipeline_skip_downstream(p, idx);
        if (sk > 0) {
            char d[NC_FMT_MAX];
            snprintf(d, sizeof(d), "skipped %d downstream", sk);
            ledger_append(&o->ledger, "fail_fast_skip", node_id, d);
        }
    }

    return -1;
}

void pipe_orch_cancel(pipe_orch_t *o) {
    cscope_request_cancel(&o->run_scope);
    ledger_append(&o->ledger, "cancel_requested", NULL, NULL);
}

void pipe_orch_cancel_node(pipe_orch_t *o, const char *node_id) {
    int idx = pipeline_find_node(o->pipeline, node_id);
    if (idx >= 0 && o->pipeline->nodes[idx].status == PIPE_RUNNING) {
        pipeline_mark_cancelled(o->pipeline, idx);
        ledger_append(&o->ledger, "node_cancelled", node_id, "explicit");
    }
    pipe_orch_clear_node_cancel(o, node_id);
}

int pipe_orch_flag_node_cancel(pipe_orch_t *o, const char *node_id) {
    if (!o || !node_id)
        return -1;
    if (pipe_orch_node_cancel_pending(o, node_id))
        return 0;
    if (o->ncancel_flags >= PIPE_ORCH_MAX_CANCEL_FLAGS)
        return -1;

    nc_strlcpy(o->cancel_flags[o->ncancel_flags], node_id, sizeof(o->cancel_flags[0]));
    o->ncancel_flags++;
    ledger_append(&o->ledger, "node_cancel_flagged", node_id, NULL);
    return 0;
}

bool pipe_orch_node_cancel_pending(const pipe_orch_t *o, const char *node_id) {
    if (!o || !node_id)
        return false;
    for (int i = 0; i < o->ncancel_flags; i++)
        if (strcmp(o->cancel_flags[i], node_id) == 0)
            return true;
    return false;
}

void pipe_orch_clear_node_cancel(pipe_orch_t *o, const char *node_id) {
    if (!o || !node_id)
        return;
    for (int i = 0; i < o->ncancel_flags; i++) {
        if (strcmp(o->cancel_flags[i], node_id) == 0) {
            for (int j = i; j < o->ncancel_flags - 1; j++)
                nc_strlcpy(o->cancel_flags[j], o->cancel_flags[j + 1], sizeof(o->cancel_flags[0]));
            o->ncancel_flags--;
            return;
        }
    }
}

int pipe_orch_queue_rerun(pipe_orch_t *o, const char *node_id) {
    if (!o || !node_id)
        return -1;
    int idx = pipeline_find_node(o->pipeline, node_id);
    if (idx < 0)
        return -1;

    for (int i = 0; i < o->nrerun_queued; i++)
        if (strcmp(o->rerun_queue[i], node_id) == 0)
            return 0;

    if (o->nrerun_queued >= PIPE_ORCH_MAX_RERUN_QUEUE)
        return -1;
    nc_strlcpy(o->rerun_queue[o->nrerun_queued], node_id, sizeof(o->rerun_queue[0]));
    o->nrerun_queued++;
    ledger_append(&o->ledger, "node_rerun_queued", node_id, NULL);
    return 0;
}

int pipe_orch_drain_reruns(pipe_orch_t *o) {
    if (!o)
        return 0;
    int drained = 0;
    pipeline_t *p = o->pipeline;

    int i = 0;
    while (i < o->nrerun_queued) {
        const char *nid = o->rerun_queue[i];
        int idx = pipeline_find_node(p, nid);
        if (idx < 0) {
            for (int j = i; j < o->nrerun_queued - 1; j++)
                nc_strlcpy(o->rerun_queue[j], o->rerun_queue[j + 1], sizeof(o->rerun_queue[0]));
            o->nrerun_queued--;
            continue;
        }
        pipe_status_t st = p->nodes[idx].status;
        if (st == PIPE_DONE || st == PIPE_FAILED || st == PIPE_SKIPPED || st == PIPE_CANCELLED) {
            p->nodes[idx].status = PIPE_PENDING;
            p->nodes[idx].finished_ms = 0;
            ledger_append(&o->ledger, "node_rerun_activated", nid, NULL);
            for (int j = i; j < o->nrerun_queued - 1; j++)
                nc_strlcpy(o->rerun_queue[j], o->rerun_queue[j + 1], sizeof(o->rerun_queue[0]));
            o->nrerun_queued--;
            drained++;
        } else {
            i++;
        }
    }
    return drained;
}

int pipe_orch_rerun_depth(const pipe_orch_t *o) {
    return o ? o->nrerun_queued : 0;
}

int pipe_orch_pad_write(pipe_orch_t *o, const char *author, const char *text) {
    return shared_pad_merge(&o->pad, author, text);
}

char *pipe_orch_pad_read(pipe_orch_t *o) {
    return shared_pad_read(&o->pad);
}

char *pipe_orch_collect_outputs(const pipe_orch_t *o) {
    strbuf_t sb;
    strbuf_init(&sb);
    const pipeline_t *p = o->pipeline;

    for (int i = 0; i < p->nnodes; i++) {
        const pipe_node_t *n = &p->nodes[i];
        if (n->status != PIPE_DONE || !n->output)
            continue;
        strbuf_appendf(&sb, "## %s\n%s\n\n", n->id, n->output);
    }
    return strbuf_detach(&sb);
}

char *pipe_orch_format_status(const pipe_orch_t *o_) {
    pipe_orch_t *o = (pipe_orch_t *)o_; /* mutex requires non-const */
    strbuf_t sb;
    strbuf_init(&sb);

    const char *state = "idle";
    switch (o->state) {
    case ORCH_RUNNING:
        state = "running";
        break;
    case ORCH_DONE:
        state = "done";
        break;
    case ORCH_FAILED:
        state = "failed";
        break;
    case ORCH_CANCELLED:
        state = "cancelled";
        break;
    default:
        break;
    }

    strbuf_appendf(&sb, "Orchestrator [%s] events=%d probes=%d cycles=%d\n", state,
                   ledger_event_count(&o->ledger), o->nprobes, o->cycles.nedges);

    if (o->started_ms > 0) {
        long elapsed = (o->finished_ms > 0 ? o->finished_ms : nc_mono_ms()) - o->started_ms;
        strbuf_appendf(&sb, "  elapsed: %ld.%lds\n", elapsed / 1000, (elapsed % 1000) / 100);
    }

    char *ps = pipeline_format_status(o->pipeline);
    if (ps) {
        strbuf_append_str(&sb, ps);
        free(ps);
    }

    int pad_lines = shared_pad_line_count(&o->pad);
    if (pad_lines > 0)
        strbuf_appendf(&sb, "  shared_pad: %d entries\n", pad_lines);

    return strbuf_detach(&sb);
}

bool pipe_orch_is_running(const pipe_orch_t *o) {
    return o->state == ORCH_RUNNING;
}

bool pipe_orch_is_terminal(const pipe_orch_t *o) {
    return o->state == ORCH_DONE || o->state == ORCH_FAILED || o->state == ORCH_CANCELLED;
}

const event_ledger_t *pipe_orch_ledger(const pipe_orch_t *o) {
    return &o->ledger;
}

void pipe_orch_attach_artifacts(pipe_orch_t *o, artifact_store_t *as) {
    o->artifacts = as;
}

void pipe_orch_enable_ctx_rendering(pipe_orch_t *o) {
    o->ctx_rendering = true;
}

char *pipe_orch_render_prompt(const pipe_orch_t *o, const char *node_id) {
    if (!o || !o->pipeline || !node_id)
        return NULL;
    int idx = pipeline_find_node(o->pipeline, node_id);
    if (idx < 0)
        return NULL;

    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    prompt_ctx_populate(&pc, o->pipeline);

    char *rendered = prompt_ctx_render(&pc, o->pipeline->nodes[idx].prompt);

    if (shared_pad_line_count((shared_pad_t *)&o->pad) > 0) {
        char *pad_content = shared_pad_read((shared_pad_t *)&o->pad);
        if (pad_content && pad_content[0] && rendered) {
            char *pad_suffix = shared_pad_build_suffix((shared_pad_t *)&o->pad);
            size_t rlen = strlen(rendered);
            size_t clen = strlen(pad_content);
            size_t slen = pad_suffix ? strlen(pad_suffix) : 0;
            char *extended = malloc(rlen + clen + slen + 4);
            if (extended) {
                memcpy(extended, rendered, rlen);
                extended[rlen] = '\n';
                memcpy(extended + rlen + 1, pad_content, clen);
                extended[rlen + 1 + clen] = '\n';
                if (slen > 0)
                    memcpy(extended + rlen + 2 + clen, pad_suffix, slen);
                extended[rlen + 2 + clen + slen] = '\0';
                free(rendered);
                rendered = extended;
            }
            free(pad_suffix);
        }
        free(pad_content);
    }

    prompt_ctx_free(&pc);
    return rendered;
}
