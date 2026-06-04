/*
 * scope_aggregate.c — Structured fanout result collection and aggregation.
 *
 * Collects outputs from expanded fanout members, computes status
 * distribution summaries, and produces filtered views (e.g. only
 * nodes that produced output, only failed nodes) for downstream
 * reducers and merge prompts.
 */

#include "harness.h"

void scope_agg_init(scope_aggregate_t *sa, const char *group_id) {
    memset(sa, 0, sizeof(*sa));
    if (group_id)
        nc_strlcpy(sa->group_id, group_id, sizeof(sa->group_id));
}

void scope_agg_free(scope_aggregate_t *sa) {
    for (int i = 0; i < sa->nmembers; i++)
        free(sa->members[i].output);
    memset(sa, 0, sizeof(*sa));
}

int scope_agg_collect(scope_aggregate_t *sa, const pipeline_t *p, const fanout_spec_t *f) {
    if (!p || !f)
        return -1;

    scope_agg_free(sa);
    nc_strlcpy(sa->group_id, f->group_id, sizeof(sa->group_id));

    for (int i = 0; i < f->nexpanded && sa->nmembers < FANOUT_MAX_COPIES; i++) {
        const char *eid = fanout_expanded_id(f, i);
        if (!eid)
            continue;

        int idx = pipeline_find_node(p, eid);
        if (idx < 0)
            continue;

        scope_member_t *m = &sa->members[sa->nmembers++];
        nc_strlcpy(m->node_id, eid, sizeof(m->node_id));
        m->status = p->nodes[idx].status;
        m->exit_code = p->nodes[idx].exit_code;
        m->attempt = p->nodes[idx].attempt;
        m->output = p->nodes[idx].output ? nc_xstrdup(p->nodes[idx].output) : NULL;
    }

    sa->summary = scope_agg_summarize(sa);
    return sa->nmembers;
}

int scope_agg_collect_prefix(scope_aggregate_t *sa, const pipeline_t *p, const char *prefix) {
    if (!p || !prefix)
        return -1;

    scope_agg_free(sa);
    nc_strlcpy(sa->group_id, prefix, sizeof(sa->group_id));
    size_t prefix_len = strlen(prefix);

    for (int i = 0; i < p->nnodes && sa->nmembers < FANOUT_MAX_COPIES; i++) {
        const pipe_node_t *n = &p->nodes[i];
        if (strncmp(n->id, prefix, prefix_len) != 0)
            continue;
        char next = n->id[prefix_len];
        if (next != '_' && next != '\0')
            continue;

        scope_member_t *m = &sa->members[sa->nmembers++];
        nc_strlcpy(m->node_id, n->id, sizeof(m->node_id));
        m->status = n->status;
        m->exit_code = n->exit_code;
        m->attempt = n->attempt;
        m->output = n->output ? nc_xstrdup(n->output) : NULL;
    }

    sa->summary = scope_agg_summarize(sa);
    return sa->nmembers;
}

scope_summary_t scope_agg_summarize(const scope_aggregate_t *sa) {
    scope_summary_t s = {0};
    s.total = sa->nmembers;

    for (int i = 0; i < sa->nmembers; i++) {
        const scope_member_t *m = &sa->members[i];
        switch (m->status) {
        case PIPE_DONE:
            s.completed++;
            break;
        case PIPE_FAILED:
            s.failed++;
            break;
        case PIPE_SKIPPED:
            s.skipped++;
            break;
        case PIPE_CANCELLED:
            s.cancelled++;
            break;
        default:
            s.pending++;
            break;
        }
        if (m->output && m->output[0])
            s.with_output++;
        else
            s.without_output++;
    }
    return s;
}

char *scope_agg_joined_outputs(const scope_aggregate_t *sa) {
    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < sa->nmembers; i++) {
        const scope_member_t *m = &sa->members[i];
        if (!m->output || !m->output[0])
            continue;
        strbuf_appendf(&sb, "## %s [%s]\n%s\n\n", m->node_id, pipe_status_name(m->status),
                       m->output);
    }
    return strbuf_detach(&sb);
}

char *scope_agg_filtered_outputs(const scope_aggregate_t *sa, pipe_status_t filter) {
    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < sa->nmembers; i++) {
        const scope_member_t *m = &sa->members[i];
        if (m->status != filter)
            continue;
        strbuf_appendf(&sb, "## %s", m->node_id);
        if (m->output && m->output[0])
            strbuf_appendf(&sb, "\n%s", m->output);
        strbuf_append_str(&sb, "\n\n");
    }
    return strbuf_detach(&sb);
}

int scope_agg_count_with_output(const scope_aggregate_t *sa) {
    int n = 0;
    for (int i = 0; i < sa->nmembers; i++)
        if (sa->members[i].output && sa->members[i].output[0])
            n++;
    return n;
}

int scope_agg_count_by_status(const scope_aggregate_t *sa, pipe_status_t s) {
    int n = 0;
    for (int i = 0; i < sa->nmembers; i++)
        if (sa->members[i].status == s)
            n++;
    return n;
}

char *scope_agg_format(const scope_aggregate_t *sa) {
    strbuf_t sb;
    strbuf_init(&sb);
    scope_summary_t s = sa->summary;

    strbuf_appendf(&sb, "Scope '%s': %d members\n", sa->group_id, s.total);
    strbuf_appendf(&sb, "  completed=%d failed=%d skipped=%d cancelled=%d pending=%d\n",
                   s.completed, s.failed, s.skipped, s.cancelled, s.pending);
    strbuf_appendf(&sb, "  with_output=%d without_output=%d\n", s.with_output, s.without_output);

    for (int i = 0; i < sa->nmembers; i++) {
        const scope_member_t *m = &sa->members[i];
        strbuf_appendf(&sb, "  [%s] %s", pipe_status_name(m->status), m->node_id);
        if (m->output && m->output[0]) {
            int len = (int)strlen(m->output);
            strbuf_appendf(&sb, " (%d chars)", len);
        }
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}

int scope_agg_subset(scope_aggregate_t *dst, const scope_aggregate_t *src, pipe_status_t filter) {
    if (!dst || !src)
        return -1;

    scope_agg_free(dst);
    nc_strlcpy(dst->group_id, src->group_id, sizeof(dst->group_id));

    for (int i = 0; i < src->nmembers && dst->nmembers < FANOUT_MAX_COPIES; i++) {
        const scope_member_t *m = &src->members[i];
        if (m->status != filter)
            continue;

        scope_member_t *d = &dst->members[dst->nmembers++];
        nc_strlcpy(d->node_id, m->node_id, sizeof(d->node_id));
        d->status = m->status;
        d->exit_code = m->exit_code;
        d->attempt = m->attempt;
        d->output = m->output ? nc_xstrdup(m->output) : NULL;
    }
    dst->summary = scope_agg_summarize(dst);
    return dst->nmembers;
}

int scope_agg_partition_output(const scope_aggregate_t *sa, scope_aggregate_t *with_out,
                               scope_aggregate_t *without_out) {
    if (!sa)
        return -1;

    if (with_out) {
        scope_agg_free(with_out);
        nc_strlcpy(with_out->group_id, sa->group_id, sizeof(with_out->group_id));
    }
    if (without_out) {
        scope_agg_free(without_out);
        nc_strlcpy(without_out->group_id, sa->group_id, sizeof(without_out->group_id));
    }

    for (int i = 0; i < sa->nmembers; i++) {
        const scope_member_t *m = &sa->members[i];
        bool has_out = (m->output && m->output[0]);
        scope_aggregate_t *tgt = has_out ? with_out : without_out;
        if (!tgt || tgt->nmembers >= FANOUT_MAX_COPIES)
            continue;

        scope_member_t *d = &tgt->members[tgt->nmembers++];
        nc_strlcpy(d->node_id, m->node_id, sizeof(d->node_id));
        d->status = m->status;
        d->exit_code = m->exit_code;
        d->attempt = m->attempt;
        d->output = m->output ? nc_xstrdup(m->output) : NULL;
    }

    if (with_out)
        with_out->summary = scope_agg_summarize(with_out);
    if (without_out)
        without_out->summary = scope_agg_summarize(without_out);
    return 0;
}

char *scope_agg_build_ctx(const scope_aggregate_t *sa) {
    strbuf_t sb;
    strbuf_init(&sb);
    scope_summary_t s = sa->summary;

    strbuf_appendf(&sb,
                   "Source: %s\n"
                   "Total: %d  Completed: %d  Failed: %d  "
                   "With output: %d  Silent: %d\n\n",
                   sa->group_id, s.total, s.completed, s.failed, s.with_output, s.without_output);

    for (int i = 0; i < sa->nmembers; i++) {
        const scope_member_t *m = &sa->members[i];
        if (!m->output || !m->output[0])
            continue;
        strbuf_appendf(&sb, "--- %s (%s) ---\n%s\n\n", m->node_id, pipe_status_name(m->status),
                       m->output);
    }

    if (s.failed > 0) {
        strbuf_append_str(&sb, "Failed members:\n");
        for (int i = 0; i < sa->nmembers; i++) {
            if (sa->members[i].status != PIPE_FAILED)
                continue;
            strbuf_appendf(&sb, "- %s (exit=%d, attempt=%d)\n", sa->members[i].node_id,
                           sa->members[i].exit_code, sa->members[i].attempt);
        }
    }
    return strbuf_detach(&sb);
}
