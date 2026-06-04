/*
 * preflight_report.c — Dry-run execution plan preview.
 *
 * Analyzes a pipeline without executing it: renders placeholder
 * prompts, detects dependency issues, warns about permissions,
 * and produces a human-readable execution plan summary.
 */

#include "harness.h"

void pflt_report_init(pflt_report_t *r) {
    memset(r, 0, sizeof(*r));
}

int pflt_add_warning(pflt_report_t *r, const char *msg) {
    if (!msg || r->nwarnings >= PFLT_MAX_WARNINGS)
        return -1;
    nc_strlcpy(r->warnings[r->nwarnings], msg, sizeof(r->warnings[0]));
    r->nwarnings++;
    return 0;
}

int pflt_build_from_pipeline(pflt_report_t *r, const pipeline_t *p) {
    if (!p)
        return -1;
    long t0 = nc_mono_ms();

    pflt_report_init(r);
    nc_strlcpy(r->pipeline_name, p->name, sizeof(r->pipeline_name));

    char err[NC_MSG_MAX];
    pipeline_t tmp;
    memcpy(&tmp, p, sizeof(tmp));
    for (int i = 0; i < tmp.nnodes; i++)
        tmp.nodes[i].output = NULL;

    if (!pipeline_validate(&tmp, err, sizeof(err))) {
        pflt_add_warning(r, err);
        r->has_cycles = true;
    }

    for (int i = 0; i < p->nnodes && r->nsteps < PFLT_MAX_STEPS; i++) {
        const pipe_node_t *n = &p->nodes[i];
        pflt_step_t *s = &r->steps[r->nsteps++];
        memset(s, 0, sizeof(*s));

        nc_strlcpy(s->node_id, n->id, sizeof(s->node_id));

        size_t plen = strlen(n->prompt);
        if (plen < PFLT_PREVIEW_MAX)
            nc_strlcpy(s->prompt_preview, n->prompt, sizeof(s->prompt_preview));
        else {
            memcpy(s->prompt_preview, n->prompt, PFLT_PREVIEW_MAX - 4);
            s->prompt_preview[PFLT_PREVIEW_MAX - 4] = '.';
            s->prompt_preview[PFLT_PREVIEW_MAX - 3] = '.';
            s->prompt_preview[PFLT_PREVIEW_MAX - 2] = '.';
            s->prompt_preview[PFLT_PREVIEW_MAX - 1] = '\0';
        }

        s->dep_count = n->ndeps;
        if (n->ndeps > 0) {
            strbuf_t sb;
            strbuf_init(&sb);
            for (int d = 0; d < n->ndeps; d++) {
                if (d)
                    strbuf_append_str(&sb, ", ");
                strbuf_append_str(&sb, n->deps[d]);
            }
            if (sb.data)
                nc_strlcpy(s->deps_str, sb.data, sizeof(s->deps_str));
            strbuf_free(&sb);
        }

        r->total_agents++;
    }

    /* Warn about nodes with many dependencies */
    for (int i = 0; i < p->nnodes; i++) {
        if (p->nodes[i].ndeps > 8) {
            char w[NC_MSG_MAX];
            snprintf(w, sizeof(w), "node '%s' has %d dependencies (may bottleneck)", p->nodes[i].id,
                     p->nodes[i].ndeps);
            pflt_add_warning(r, w);
        }
    }

    /* Warn about orphan nodes (no deps and nothing depends on them) */
    for (int i = 0; i < p->nnodes; i++) {
        if (p->nodes[i].ndeps > 0)
            continue;
        bool depended = false;
        for (int j = 0; j < p->nnodes && !depended; j++) {
            for (int d = 0; d < p->nodes[j].ndeps; d++) {
                if (strcmp(p->nodes[j].deps[d], p->nodes[i].id) == 0) {
                    depended = true;
                    break;
                }
            }
        }
        if (!depended && p->nnodes > 1) {
            char w[NC_MSG_MAX];
            snprintf(w, sizeof(w), "node '%s' is isolated (no dependencies in or out)",
                     p->nodes[i].id);
            pflt_add_warning(r, w);
        }
    }

    r->build_ms = nc_mono_ms() - t0;
    return 0;
}

char *pflt_format_report(const pflt_report_t *r) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_appendf(&sb, "Preflight: '%s' (%d nodes, %ldms)\n", r->pipeline_name, r->nsteps,
                   r->build_ms);

    if (r->has_cycles)
        strbuf_append_str(&sb, "  *** CYCLE DETECTED ***\n");

    for (int i = 0; i < r->nwarnings; i++)
        strbuf_appendf(&sb, "  Warning: %s\n", r->warnings[i]);

    strbuf_append_str(&sb, "  Execution order:\n");
    for (int i = 0; i < r->nsteps; i++) {
        const pflt_step_t *s = &r->steps[i];
        strbuf_appendf(&sb, "    %d. %s", i + 1, s->node_id);
        if (s->dep_count > 0)
            strbuf_appendf(&sb, " (after: %s)", s->deps_str);
        strbuf_append_str(&sb, "\n");
        strbuf_appendf(&sb, "       Prompt: %.*s%s\n", PREFLIGHT_PREVIEW_MAX, s->prompt_preview,
                       strlen(s->prompt_preview) > PREFLIGHT_PREVIEW_MAX ? "..." : "");
    }
    return strbuf_detach(&sb);
}

char *pflt_format_summary(const pflt_report_t *r) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Pipeline '%s': %d nodes, %d warnings", r->pipeline_name, r->nsteps,
                   r->nwarnings);
    if (r->has_cycles)
        strbuf_append_str(&sb, " [HAS CYCLES]");
    return strbuf_detach(&sb);
}

bool pflt_has_issues(const pflt_report_t *r) {
    return r->has_cycles || r->nwarnings > 0;
}
