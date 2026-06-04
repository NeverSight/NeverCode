/*
 * retry_cycle.c — Controlled failure back-edge iteration loops.
 *
 * Registers directed back-edges from tail nodes to earlier targets.
 * When a tail node fails and the iteration limit is not exhausted,
 * the targets (and all intermediate nodes) are reset to pending,
 * forming a write-test-fix loop with bounded retries.
 */

#include "harness.h"

void rcycle_init(retry_cycle_t *rc, int max_iters) {
    memset(rc, 0, sizeof(*rc));
    rc->max_iterations = (max_iters > 0) ? max_iters : RCYCLE_DEFAULT_MAX_ITERS;
}

void rcycle_free(retry_cycle_t *rc) {
    memset(rc, 0, sizeof(*rc));
}

static int _find_edge(const retry_cycle_t *rc, const char *tail_id) {
    for (int i = 0; i < rc->nedges; i++)
        if (strcmp(rc->edges[i].tail_id, tail_id) == 0)
            return i;
    return -1;
}

int rcycle_add_edge(retry_cycle_t *rc, const char *tail_id, const char **targets, int ntargets) {
    if (!tail_id || !targets || ntargets < 1)
        return -1;
    if (rc->nedges >= RCYCLE_MAX_EDGES)
        return -1;
    if (ntargets > RCYCLE_MAX_CHAIN)
        ntargets = RCYCLE_MAX_CHAIN;

    rcycle_edge_t *e = &rc->edges[rc->nedges];
    nc_strlcpy(e->tail_id, tail_id, sizeof(e->tail_id));
    e->ntargets = 0;
    for (int i = 0; i < ntargets; i++) {
        if (targets[i])
            nc_strlcpy(e->targets[e->ntargets++], targets[i], sizeof(e->targets[0]));
    }
    rc->nedges++;
    return 0;
}

bool rcycle_should_restart(const retry_cycle_t *rc, const char *failed_node) {
    int idx = _find_edge(rc, failed_node);
    if (idx < 0)
        return false;
    return rc->iteration_counts[idx] < rc->max_iterations;
}

static void _reset_node(pipeline_t *p, int idx) {
    if (idx < 0 || idx >= p->nnodes)
        return;
    p->nodes[idx].status = PIPE_PENDING;
    p->nodes[idx].finished_ms = 0;
    free(p->nodes[idx].output);
    p->nodes[idx].output = NULL;
}

/* BFS to find all nodes on paths between any target and the tail */
static int _collect_intermediates(const pipeline_t *p, const char *target, const char *tail,
                                  int *out, int max) {
    bool reverse[PIPE_MAX_NODES];
    memset(reverse, 0, sizeof(reverse));
    int target_idx = pipeline_find_node(p, target);
    int tail_idx = pipeline_find_node(p, tail);
    if (target_idx < 0 || tail_idx < 0)
        return 0;

    int queue[PIPE_MAX_NODES], qh = 0, qt = 0;
    queue[qt++] = target_idx;
    reverse[target_idx] = true;
    int found = 0;

    while (qh < qt) {
        int cur = queue[qh++];
        for (int i = 0; i < p->nnodes; i++) {
            if (reverse[i])
                continue;
            for (int d = 0; d < p->nodes[i].ndeps; d++) {
                int dep_idx = pipeline_find_node(p, p->nodes[i].deps[d]);
                if (dep_idx == cur) {
                    reverse[i] = true;
                    if (i != tail_idx && found < max)
                        out[found++] = i;
                    queue[qt++] = i;
                    break;
                }
            }
        }
    }
    return found;
}

int rcycle_do_restart(retry_cycle_t *rc, const char *failed_node, pipeline_t *p) {
    int edge_idx = _find_edge(rc, failed_node);
    if (edge_idx < 0)
        return -1;
    if (rc->iteration_counts[edge_idx] >= rc->max_iterations)
        return -1;

    rc->iteration_counts[edge_idx]++;

    /* Reset the tail node itself */
    int tail = pipeline_find_node(p, failed_node);
    _reset_node(p, tail);

    /* Reset each target and intermediate nodes */
    const rcycle_edge_t *e = &rc->edges[edge_idx];
    for (int t = 0; t < e->ntargets; t++) {
        int tidx = pipeline_find_node(p, e->targets[t]);
        _reset_node(p, tidx);

        int intermediates[PIPE_MAX_NODES];
        int ni =
            _collect_intermediates(p, e->targets[t], failed_node, intermediates, PIPE_MAX_NODES);
        for (int m = 0; m < ni; m++)
            _reset_node(p, intermediates[m]);
    }
    return rc->iteration_counts[edge_idx];
}

int rcycle_iteration(const retry_cycle_t *rc, const char *tail_id) {
    int idx = _find_edge(rc, tail_id);
    if (idx < 0)
        return 0;
    return rc->iteration_counts[idx];
}

bool rcycle_exhausted(const retry_cycle_t *rc, const char *tail_id) {
    int idx = _find_edge(rc, tail_id);
    if (idx < 0)
        return true;
    return rc->iteration_counts[idx] >= rc->max_iterations;
}

char *rcycle_format_status(const retry_cycle_t *rc) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Retry cycles: %d edges, max_iterations=%d\n", rc->nedges,
                   rc->max_iterations);

    for (int i = 0; i < rc->nedges; i++) {
        const rcycle_edge_t *e = &rc->edges[i];
        strbuf_appendf(&sb, "  %s (iter %d/%d) ->", e->tail_id, rc->iteration_counts[i],
                       rc->max_iterations);
        for (int t = 0; t < e->ntargets; t++) {
            strbuf_appendf(&sb, " %s", e->targets[t]);
            if (t < e->ntargets - 1)
                strbuf_append_str(&sb, ",");
        }
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}
