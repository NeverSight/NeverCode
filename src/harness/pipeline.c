/*
 * pipeline.c — Declarative dependency graph for multi-step agent tasks.
 *
 * Nodes declare upstream dependencies; the scheduler drives them to
 * completion in topological order with bounded concurrency.  Downstream
 * nodes of a failure are skipped unless fail_fast is off and a retry
 * cycle is configured.
 */

#include "harness.h"

void pipeline_init(pipeline_t *p, const char *name, int concurrency) {
    memset(p, 0, sizeof(*p));
    if (name)
        nc_strlcpy(p->name, name, sizeof(p->name));
    p->concurrency = (concurrency > 0) ? concurrency : PIPE_MAX_CONCURRENT;
    if (p->concurrency > PIPE_MAX_CONCURRENT)
        p->concurrency = PIPE_MAX_CONCURRENT;
    p->max_iterations = RCYCLE_DEFAULT_MAX_ITERS;
    p->status = PIPE_PENDING;
}

void pipeline_free(pipeline_t *p) {
    for (int i = 0; i < p->nnodes; i++)
        free(p->nodes[i].output);
    memset(p, 0, sizeof(*p));
}

int pipeline_find_node(const pipeline_t *p, const char *id) {
    if (!id)
        return -1;
    for (int i = 0; i < p->nnodes; i++)
        if (strcmp(p->nodes[i].id, id) == 0)
            return i;
    return -1;
}

int pipeline_add_node(pipeline_t *p, const char *id, const char *prompt) {
    if (!id || !prompt || p->nnodes >= PIPE_MAX_NODES)
        return -1;
    if (pipeline_find_node(p, id) >= 0)
        return -1;

    pipe_node_t *n = &p->nodes[p->nnodes];
    memset(n, 0, sizeof(*n));
    nc_strlcpy(n->id, id, sizeof(n->id));
    nc_strlcpy(n->prompt, prompt, sizeof(n->prompt));
    n->status = PIPE_PENDING;
    n->retries = 0;
    return p->nnodes++;
}

int pipeline_add_dep(pipeline_t *p, const char *node_id, const char *dep_id) {
    int idx = pipeline_find_node(p, node_id);
    if (idx < 0)
        return -1;
    if (pipeline_find_node(p, dep_id) < 0)
        return -1;

    pipe_node_t *n = &p->nodes[idx];
    if (n->ndeps >= PIPE_MAX_DEPS)
        return -1;

    for (int i = 0; i < n->ndeps; i++)
        if (strcmp(n->deps[i], dep_id) == 0)
            return 0;

    nc_strlcpy(n->deps[n->ndeps], dep_id, sizeof(n->deps[0]));
    n->ndeps++;
    return 0;
}

int pipeline_set_retries(pipeline_t *p, const char *node_id, int retries) {
    int idx = pipeline_find_node(p, node_id);
    if (idx < 0)
        return -1;
    p->nodes[idx].retries = retries > 0 ? retries : 0;
    return 0;
}

static bool _deps_all_done(const pipeline_t *p, const pipe_node_t *n) {
    for (int i = 0; i < n->ndeps; i++) {
        int dep = pipeline_find_node(p, n->deps[i]);
        if (dep < 0 || p->nodes[dep].status != PIPE_DONE)
            return false;
    }
    return true;
}

static bool _any_dep_failed(const pipeline_t *p, const pipe_node_t *n) {
    for (int i = 0; i < n->ndeps; i++) {
        int dep = pipeline_find_node(p, n->deps[i]);
        if (dep >= 0) {
            pipe_status_t s = p->nodes[dep].status;
            if (s == PIPE_FAILED || s == PIPE_SKIPPED || s == PIPE_CANCELLED)
                return true;
        }
    }
    return false;
}

static bool _is_terminal(pipe_status_t s) {
    return s == PIPE_DONE || s == PIPE_FAILED || s == PIPE_SKIPPED || s == PIPE_CANCELLED;
}

bool pipeline_validate(const pipeline_t *p, char *err, size_t errsz) {
    if (p->nnodes == 0) {
        if (err)
            snprintf(err, errsz, "pipeline has no nodes");
        return false;
    }
    for (int i = 0; i < p->nnodes; i++) {
        for (int j = 0; j < p->nodes[i].ndeps; j++) {
            if (pipeline_find_node(p, p->nodes[i].deps[j]) < 0) {
                if (err)
                    snprintf(err, errsz, "node '%s' references unknown dep '%s'", p->nodes[i].id,
                             p->nodes[i].deps[j]);
                return false;
            }
        }
    }

    /* cycle detection via DFS coloring */
    int color[PIPE_MAX_NODES];
    memset(color, 0, sizeof(color));
    for (int start = 0; start < p->nnodes; start++) {
        if (color[start] != 0)
            continue;
        int stack[PIPE_MAX_NODES];
        int sp = 0;
        stack[sp++] = start;
        color[start] = 1;
        while (sp > 0) {
            int cur = stack[sp - 1];
            bool pushed = false;
            for (int d = 0; d < p->nodes[cur].ndeps; d++) {
                int dep = pipeline_find_node(p, p->nodes[cur].deps[d]);
                if (dep < 0)
                    continue;
                if (color[dep] == 1) {
                    if (err)
                        snprintf(err, errsz, "cycle detected involving '%s'", p->nodes[dep].id);
                    return false;
                }
                if (color[dep] == 0) {
                    color[dep] = 1;
                    stack[sp++] = dep;
                    pushed = true;
                    break;
                }
            }
            if (!pushed) {
                color[cur] = 2;
                sp--;
            }
        }
    }
    return true;
}

int pipeline_ready_nodes(const pipeline_t *p, int *out, int max) {
    int n = 0;
    for (int i = 0; i < p->nnodes && n < max; i++) {
        if (p->nodes[i].status != PIPE_PENDING)
            continue;
        if (_deps_all_done(p, &p->nodes[i]))
            out[n++] = i;
    }
    return n;
}

int pipeline_mark_running(pipeline_t *p, int idx) {
    if (idx < 0 || idx >= p->nnodes)
        return -1;
    p->nodes[idx].status = PIPE_RUNNING;
    p->nodes[idx].started_ms = nc_mono_ms();
    p->nodes[idx].attempt++;
    if (p->status == PIPE_PENDING) {
        p->status = PIPE_RUNNING;
        p->started_ms = nc_mono_ms();
    }
    return 0;
}

int pipeline_mark_done(pipeline_t *p, int idx, const char *output, int exit_code) {
    if (idx < 0 || idx >= p->nnodes)
        return -1;
    pipe_node_t *n = &p->nodes[idx];
    n->status = PIPE_DONE;
    n->exit_code = exit_code;
    n->finished_ms = nc_mono_ms();
    free(n->output);
    n->output = output ? nc_xstrdup(output) : NULL;
    return 0;
}

int pipeline_mark_failed(pipeline_t *p, int idx, const char *output, int exit_code) {
    if (idx < 0 || idx >= p->nnodes)
        return -1;
    pipe_node_t *n = &p->nodes[idx];
    n->status = PIPE_FAILED;
    n->exit_code = exit_code;
    n->finished_ms = nc_mono_ms();
    free(n->output);
    n->output = output ? nc_xstrdup(output) : NULL;
    return 0;
}

int pipeline_mark_cancelled(pipeline_t *p, int idx) {
    if (idx < 0 || idx >= p->nnodes)
        return -1;
    p->nodes[idx].status = PIPE_CANCELLED;
    p->nodes[idx].finished_ms = nc_mono_ms();
    return 0;
}

int pipeline_skip_downstream(pipeline_t *p, int failed_idx) {
    if (failed_idx < 0 || failed_idx >= p->nnodes)
        return 0;
    int skipped = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < p->nnodes; i++) {
            if (p->nodes[i].status != PIPE_PENDING)
                continue;
            if (_any_dep_failed(p, &p->nodes[i])) {
                p->nodes[i].status = PIPE_SKIPPED;
                p->nodes[i].finished_ms = nc_mono_ms();
                skipped++;
                changed = true;
            }
        }
    }
    return skipped;
}

bool pipeline_all_terminal(const pipeline_t *p) {
    for (int i = 0; i < p->nnodes; i++)
        if (!_is_terminal(p->nodes[i].status))
            return false;
    return true;
}

const char *pipe_status_name(pipe_status_t s) {
    switch (s) {
    case PIPE_PENDING:
        return "pending";
    case PIPE_QUEUED:
        return "queued";
    case PIPE_RUNNING:
        return "running";
    case PIPE_DONE:
        return "done";
    case PIPE_FAILED:
        return "failed";
    case PIPE_SKIPPED:
        return "skipped";
    case PIPE_CANCELLED:
        return "cancelled";
    }
    return "unknown";
}

char *pipeline_format_status(const pipeline_t *p) {
    strbuf_t sb;
    strbuf_init(&sb);
    int counts[7] = {0};
    for (int i = 0; i < p->nnodes; i++)
        counts[p->nodes[i].status]++;

    strbuf_appendf(&sb, "Pipeline '%s' [%s] nodes=%d concurrency=%d\n", p->name,
                   pipe_status_name(p->status), p->nnodes, p->concurrency);
    strbuf_appendf(&sb, "  pending=%d running=%d done=%d failed=%d skipped=%d\n",
                   counts[PIPE_PENDING], counts[PIPE_RUNNING], counts[PIPE_DONE],
                   counts[PIPE_FAILED], counts[PIPE_SKIPPED]);

    for (int i = 0; i < p->nnodes; i++) {
        const pipe_node_t *n = &p->nodes[i];
        strbuf_appendf(&sb, "  [%s] %s", pipe_status_name(n->status), n->id);
        if (n->ndeps > 0) {
            strbuf_append_str(&sb, " <- ");
            for (int d = 0; d < n->ndeps; d++) {
                if (d)
                    strbuf_append_str(&sb, ", ");
                strbuf_append_str(&sb, n->deps[d]);
            }
        }
        if (n->attempt > 1)
            strbuf_appendf(&sb, " (attempt %d)", n->attempt);
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}
