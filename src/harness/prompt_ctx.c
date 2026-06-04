/*
 * prompt_ctx.c — Template rendering with upstream node context.
 *
 * Builds a key-value context from pipeline node results and renders
 * templates using {{ key }} interpolation.  Downstream nodes can
 * reference upstream outputs via {{ nodeid.output }}, status via
 * {{ nodeid.status }}, and scope aggregates via {{ scope.summary }}.
 */

#include "harness.h"

void prompt_ctx_init(prompt_ctx_t *pc) {
    memset(pc, 0, sizeof(*pc));
}

void prompt_ctx_free(prompt_ctx_t *pc) {
    for (int i = 0; i < pc->nvars; i++)
        free(pc->vars[i].value);
    memset(pc, 0, sizeof(*pc));
}

static int _find_var(const prompt_ctx_t *pc, const char *key) {
    for (int i = 0; i < pc->nvars; i++)
        if (strcmp(pc->vars[i].key, key) == 0)
            return i;
    return -1;
}

int prompt_ctx_set(prompt_ctx_t *pc, const char *key, const char *value) {
    if (!key || !key[0])
        return -1;

    int idx = _find_var(pc, key);
    if (idx >= 0) {
        free(pc->vars[idx].value);
        pc->vars[idx].value = value ? nc_xstrdup(value) : NULL;
        return 0;
    }
    if (pc->nvars >= PROMPT_CTX_MAX_VARS)
        return -1;

    prompt_ctx_var_t *v = &pc->vars[pc->nvars++];
    nc_strlcpy(v->key, key, sizeof(v->key));
    v->value = value ? nc_xstrdup(value) : NULL;
    return 0;
}

const char *prompt_ctx_get(const prompt_ctx_t *pc, const char *key) {
    int idx = _find_var(pc, key);
    if (idx < 0)
        return NULL;
    return pc->vars[idx].value;
}

int prompt_ctx_populate(prompt_ctx_t *pc, const pipeline_t *p) {
    if (!p)
        return -1;
    int added = 0;

    for (int i = 0; i < p->nnodes; i++) {
        const pipe_node_t *n = &p->nodes[i];
        char key[PROMPT_CTX_KEY_MAX];

        snprintf(key, sizeof(key), "%s.output", n->id);
        prompt_ctx_set(pc, key, n->output ? n->output : "");
        added++;

        snprintf(key, sizeof(key), "%s.status", n->id);
        prompt_ctx_set(pc, key, pipe_status_name(n->status));
        added++;

        snprintf(key, sizeof(key), "%s.exit_code", n->id);
        char val[NC_SHORT_MAX];
        snprintf(val, sizeof(val), "%d", n->exit_code);
        prompt_ctx_set(pc, key, val);
        added++;

        snprintf(key, sizeof(key), "%s.attempt", n->id);
        snprintf(val, sizeof(val), "%d", n->attempt);
        prompt_ctx_set(pc, key, val);
        added++;
    }

    char val[NC_SHORT_MAX];
    snprintf(val, sizeof(val), "%d", p->nnodes);
    prompt_ctx_set(pc, "pipeline.node_count", val);
    prompt_ctx_set(pc, "pipeline.name", p->name);

    int done = 0, failed = 0;
    for (int i = 0; i < p->nnodes; i++) {
        if (p->nodes[i].status == PIPE_DONE)
            done++;
        if (p->nodes[i].status == PIPE_FAILED)
            failed++;
    }
    snprintf(val, sizeof(val), "%d", done);
    prompt_ctx_set(pc, "pipeline.completed", val);
    snprintf(val, sizeof(val), "%d", failed);
    prompt_ctx_set(pc, "pipeline.failed", val);

    return added;
}

int prompt_ctx_populate_scope(prompt_ctx_t *pc, const char *prefix, const scope_aggregate_t *sa) {
    if (!pc || !prefix || !sa)
        return -1;
    int added = 0;
    char key[PROMPT_CTX_KEY_MAX], val[64];

    scope_summary_t s = sa->summary;

    snprintf(key, sizeof(key), "%s.total", prefix);
    snprintf(val, sizeof(val), "%d", s.total);
    prompt_ctx_set(pc, key, val);
    added++;

    snprintf(key, sizeof(key), "%s.completed", prefix);
    snprintf(val, sizeof(val), "%d", s.completed);
    prompt_ctx_set(pc, key, val);
    added++;

    snprintf(key, sizeof(key), "%s.failed", prefix);
    snprintf(val, sizeof(val), "%d", s.failed);
    prompt_ctx_set(pc, key, val);
    added++;

    snprintf(key, sizeof(key), "%s.with_output", prefix);
    snprintf(val, sizeof(val), "%d", s.with_output);
    prompt_ctx_set(pc, key, val);
    added++;

    snprintf(key, sizeof(key), "%s.without_output", prefix);
    snprintf(val, sizeof(val), "%d", s.without_output);
    prompt_ctx_set(pc, key, val);
    added++;

    char *ctx_text = scope_agg_build_ctx(sa);
    if (ctx_text) {
        snprintf(key, sizeof(key), "%s.context", prefix);
        prompt_ctx_set(pc, key, ctx_text);
        free(ctx_text);
        added++;
    }

    char *outputs = scope_agg_joined_outputs(sa);
    if (outputs) {
        snprintf(key, sizeof(key), "%s.outputs", prefix);
        prompt_ctx_set(pc, key, outputs);
        free(outputs);
        added++;
    }

    return added;
}

int prompt_ctx_populate_filtered(prompt_ctx_t *pc, const char *prefix, const scope_aggregate_t *sa,
                                 pipe_status_t filter) {
    if (!pc || !prefix || !sa)
        return -1;

    scope_aggregate_t filtered;
    scope_agg_init(&filtered, sa->group_id);
    scope_agg_subset(&filtered, sa, filter);

    int added = prompt_ctx_populate_scope(pc, prefix, &filtered);
    scope_agg_free(&filtered);
    return added;
}

static bool _is_var_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.';
}

char *prompt_ctx_render(const prompt_ctx_t *pc, const char *tpl) {
    if (!tpl)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);

    const char *p = tpl;
    while (*p) {
        if (p[0] == '{' && p[1] == '{') {
            const char *start = p + 2;
            while (*start == ' ')
                start++;

            const char *end = start;
            while (*end && _is_var_char(*end))
                end++;

            const char *close = end;
            while (*close == ' ')
                close++;

            if (close[0] == '}' && close[1] == '}' && end > start) {
                char key[PROMPT_CTX_KEY_MAX];
                size_t klen = (size_t)(end - start);
                if (klen >= sizeof(key))
                    klen = sizeof(key) - 1;
                memcpy(key, start, klen);
                key[klen] = '\0';

                const char *val = prompt_ctx_get(pc, key);
                if (val)
                    strbuf_append_str(&sb, val);
                else
                    strbuf_appendf(&sb, "{{%s}}", key);

                p = close + 2;
                continue;
            }
        }

        const char *run = p;
        while (*run && !(run[0] == '{' && run[1] == '{'))
            run++;
        if (run > p) {
            size_t span = (size_t)(run - p);
            char *chunk = malloc(span + 1);
            if (chunk) {
                memcpy(chunk, p, span);
                chunk[span] = '\0';
                strbuf_append_str(&sb, chunk);
                free(chunk);
            }
            p = run;
        } else {
            char c[2] = {*p, '\0'};
            strbuf_append_str(&sb, c);
            p++;
        }
    }
    return strbuf_detach(&sb);
}
