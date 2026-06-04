/*
 * periodic_probe.c — Tick-based health monitor for parallel agent groups.
 *
 * A probe watches a fan-out group and fires at regular intervals.
 * Each tick can emit control actions (cancel stalled agents, restart
 * failed ones) which the pipeline scheduler applies cooperatively.
 */

#include "harness.h"

void probe_init(periodic_probe_t *pp, const char *group, int interval_s) {
    memset(pp, 0, sizeof(*pp));
    if (group)
        nc_strlcpy(pp->watch_group, group, sizeof(pp->watch_group));
    pp->interval_s = (interval_s > 0) ? interval_s : PROBE_TICK_DEFAULT_S;
    pp->state = PROBE_IDLE;
    pp->next_tick_ms = nc_mono_ms() + (long)pp->interval_s * 1000L;
}

void probe_free(periodic_probe_t *pp) {
    memset(pp, 0, sizeof(*pp));
}

void probe_snapshot(const periodic_probe_t *pp, probe_snapshot_t *out) {
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    if (!pp)
        return;

    long now = nc_mono_ms();
    nc_strlcpy(out->watch_group, pp->watch_group, sizeof(out->watch_group));
    out->interval_s = pp->interval_s;
    out->tick_count = pp->tick_count;
    out->state = pp->state;
    if (pp->last_tick_ms > 0)
        out->last_tick_age_ms = NC_MAX(0, now - pp->last_tick_ms);
    out->next_tick_delay_ms = NC_MAX(0, pp->next_tick_ms - now);
    out->nactions = pp->nactions;
    if (out->nactions < 0)
        out->nactions = 0;
    if (out->nactions > PROBE_MAX_ACTIONS)
        out->nactions = PROBE_MAX_ACTIONS;
    for (int i = 0; i < out->nactions; i++)
        out->actions[i] = pp->actions[i];
}

void probe_restore(periodic_probe_t *pp, const probe_snapshot_t *snap) {
    if (!pp)
        return;
    if (!snap) {
        probe_init(pp, NULL, 0);
        return;
    }

    probe_init(pp, snap->watch_group, snap->interval_s);

    long now = nc_mono_ms();
    long age_ms = NC_MAX(0, snap->last_tick_age_ms);
    long delay_ms = NC_MAX(0, snap->next_tick_delay_ms);

    pp->tick_count = NC_MAX(0, snap->tick_count);
    if (snap->state >= PROBE_IDLE && snap->state <= PROBE_DONE)
        pp->state = snap->state;
    else
        pp->state = PROBE_IDLE;

    if (age_ms > 0 && age_ms <= now)
        pp->last_tick_ms = now - age_ms;
    else
        pp->last_tick_ms = 0;

    if (pp->state == PROBE_DONE)
        pp->next_tick_ms = 0;
    else
        pp->next_tick_ms = now + delay_ms;

    pp->nactions = snap->nactions;
    if (pp->nactions < 0)
        pp->nactions = 0;
    if (pp->nactions > PROBE_MAX_ACTIONS)
        pp->nactions = PROBE_MAX_ACTIONS;
    for (int i = 0; i < pp->nactions; i++)
        pp->actions[i] = snap->actions[i];
}

bool probe_should_tick(const periodic_probe_t *pp, long now_ms) {
    if (pp->state == PROBE_DONE)
        return false;
    return now_ms >= pp->next_tick_ms;
}

int probe_record_tick(periodic_probe_t *pp) {
    long now = nc_mono_ms();
    pp->tick_count++;
    pp->last_tick_ms = now;
    pp->next_tick_ms = now + (long)pp->interval_s * 1000L;
    pp->nactions = 0;
    if (pp->state == PROBE_IDLE)
        pp->state = PROBE_WATCHING;
    return pp->tick_count;
}

int probe_add_action(periodic_probe_t *pp, probe_action_kind_t kind, const char *target,
                     const char *reason) {
    if (pp->nactions >= PROBE_MAX_ACTIONS)
        return -1;

    probe_action_t *a = &pp->actions[pp->nactions++];
    a->kind = kind;
    if (target)
        nc_strlcpy(a->target_id, target, sizeof(a->target_id));
    if (reason)
        nc_strlcpy(a->reason, reason, sizeof(a->reason));
    return 0;
}

int probe_apply_actions(periodic_probe_t *pp, pipeline_t *p) {
    if (!p)
        return -1;
    int applied = 0;

    for (int i = 0; i < pp->nactions; i++) {
        probe_action_t *a = &pp->actions[i];
        int idx = pipeline_find_node(p, a->target_id);
        if (idx < 0)
            continue;

        switch (a->kind) {
        case PROBE_ACT_CANCEL:
            if (p->nodes[idx].status == PIPE_RUNNING || p->nodes[idx].status == PIPE_QUEUED) {
                pipeline_mark_cancelled(p, idx);
                applied++;
            }
            break;

        case PROBE_ACT_RESTART:
            if (p->nodes[idx].status == PIPE_FAILED || p->nodes[idx].status == PIPE_CANCELLED) {
                p->nodes[idx].status = PIPE_PENDING;
                p->nodes[idx].finished_ms = 0;
                free(p->nodes[idx].output);
                p->nodes[idx].output = NULL;
                applied++;
            }
            break;

        case PROBE_ACT_NONE:
            break;
        }
    }
    return applied;
}

bool probe_group_settled(const periodic_probe_t *pp, const pipeline_t *p) {
    if (!p || !pp->watch_group[0])
        return true;

    size_t prefix_len = strlen(pp->watch_group);
    for (int i = 0; i < p->nnodes; i++) {
        if (strncmp(p->nodes[i].id, pp->watch_group, prefix_len) != 0)
            continue;
        char next = p->nodes[i].id[prefix_len];
        if (next != '_' && next != '\0')
            continue;

        pipe_status_t s = p->nodes[i].status;
        if (s == PIPE_PENDING || s == PIPE_QUEUED || s == PIPE_RUNNING)
            return false;
    }
    return true;
}

void probe_finalize(periodic_probe_t *pp) {
    pp->state = PROBE_DONE;
    pp->nactions = 0;
}

char *probe_format_status(const periodic_probe_t *pp) {
    strbuf_t sb;
    strbuf_init(&sb);

    const char *state = "idle";
    switch (pp->state) {
    case PROBE_WATCHING:
        state = "watching";
        break;
    case PROBE_DONE:
        state = "done";
        break;
    default:
        break;
    }

    strbuf_appendf(&sb, "Probe '%s' [%s] ticks=%d interval=%ds", pp->watch_group, state,
                   pp->tick_count, pp->interval_s);

    if (pp->nactions > 0) {
        strbuf_appendf(&sb, " pending_actions=%d", pp->nactions);
        for (int i = 0; i < pp->nactions; i++) {
            const char *kind = pp->actions[i].kind == PROBE_ACT_CANCEL ? "cancel" : "restart";
            strbuf_appendf(&sb, "\n  %s -> %s", kind, pp->actions[i].target_id);
            if (pp->actions[i].reason[0])
                strbuf_appendf(&sb, " (%s)", pp->actions[i].reason);
        }
    }
    return strbuf_detach(&sb);
}
