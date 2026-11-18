/*
 * fanout.c — MapReduce primitives for parallel agent coordination.
 *
 * Expands a single template node into many parallel copies (fan-out),
 * then groups or batches their results for staged reduction (merge).
 */

#include "harness.h"

/* Width of the numeric suffix used in generated ids: digits needed so that
 * lexical id ordering matches numeric ordering (clamped to [1, 9]). */
static int _suffix_width(int total) {
    int w = 1;
    while (total >= 10 && w < 9) {
        total /= 10;
        w++;
    }
    return w;
}

void fanout_init(fanout_spec_t *f, const char *group_id, const char *tpl) {
    memset(f, 0, sizeof(*f));
    if (group_id)
        nc_strlcpy(f->group_id, group_id, sizeof(f->group_id));
    if (tpl)
        nc_strlcpy(f->template_prompt, tpl, sizeof(f->template_prompt));
    f->mode = FANOUT_COUNT;
}

void fanout_free(fanout_spec_t *f) {
    for (int i = 0; i < f->nvalues; i++)
        free(f->values[i]);
    memset(f, 0, sizeof(*f));
}

int fanout_set_count(fanout_spec_t *f, int n) {
    if (n < 1 || n > FANOUT_MAX_COPIES)
        return -1;
    f->mode = FANOUT_COUNT;
    f->count = n;
    return 0;
}

int fanout_add_value(fanout_spec_t *f, const char *val) {
    if (!val || f->nvalues >= FANOUT_MAX_COPIES)
        return -1;
    f->mode = FANOUT_VALUES;
    f->values[f->nvalues++] = nc_xstrdup(val);
    return 0;
}

int fanout_expand(fanout_spec_t *f, pipeline_t *p) {
    int total = (f->mode == FANOUT_VALUES) ? f->nvalues : f->count;
    if (total < 1 || total > FANOUT_MAX_COPIES)
        return -1;

    int width = _suffix_width(total);
    width = width > 9 ? 9 : width;
    f->nexpanded = 0;

    for (int i = 0; i < total; i++) {
        char id[PIPE_ID_MAX];
        snprintf(id, sizeof(id), "%.52s_%0*d", f->group_id, width, i);
        nc_strlcpy(f->expanded_ids[f->nexpanded], id, sizeof(f->expanded_ids[0]));
        f->nexpanded++;

        if (p) {
            char prompt[PIPE_PROMPT_MAX];
            if (f->mode == FANOUT_VALUES && f->values[i])
                snprintf(prompt, sizeof(prompt), "%.700s\n\nItem: %.200s\nIndex: %d/%d",
                         f->template_prompt, f->values[i], i + 1, total);
            else
                snprintf(prompt, sizeof(prompt), "%.760s\n\nShard: %d/%d  ID: %s",
                         f->template_prompt, i + 1, total, id);

            pipeline_add_node(p, id, prompt);
        }
    }
    return f->nexpanded;
}

int fanout_expanded_count(const fanout_spec_t *f) {
    return f->nexpanded;
}

const char *fanout_expanded_id(const fanout_spec_t *f, int idx) {
    if (idx < 0 || idx >= f->nexpanded)
        return NULL;
    return f->expanded_ids[idx];
}

void merge_init(merge_spec_t *m, const char *group_id, const char *source) {
    memset(m, 0, sizeof(*m));
    if (group_id)
        nc_strlcpy(m->group_id, group_id, sizeof(m->group_id));
    if (source)
        nc_strlcpy(m->source_group, source, sizeof(m->source_group));
    m->mode = MERGE_BATCH;
    m->batch_size = FANOUT_BATCH_MAX;
}

int merge_set_batch(merge_spec_t *m, int size) {
    if (size < 1)
        return -1;
    m->mode = MERGE_BATCH;
    m->batch_size = size;
    return 0;
}

int merge_set_group(merge_spec_t *m, const char *field) {
    if (!field)
        return -1;
    m->mode = MERGE_GROUP;
    nc_strlcpy(m->group_field, field, sizeof(m->group_field));
    return 0;
}

int merge_expand(merge_spec_t *m, const fanout_spec_t *src, pipeline_t *p, const char *tpl) {
    if (!src || !p || !tpl)
        return -1;
    int total = src->nexpanded;
    if (total < 1)
        return -1;

    int batch_count = 0;
    if (m->mode == MERGE_BATCH) {
        batch_count = (total + m->batch_size - 1) / m->batch_size;
    } else {
        batch_count = total;
    }

    int width = _suffix_width(batch_count);
    width = width > 9 ? 9 : width;
    m->nbatches = 0;

    for (int b = 0; b < batch_count && m->nbatches < FANOUT_MAX_COPIES; b++) {
        char id[PIPE_ID_MAX];
        snprintf(id, sizeof(id), "%.52s_%0*d", m->group_id, width, b);

        int start = b * m->batch_size;
        int end = start + m->batch_size;
        if (end > total)
            end = total;

        char prompt[PIPE_PROMPT_MAX];
        snprintf(prompt, sizeof(prompt), "%.640s\n\nBatch: %d/%d  Source: %.80s  Members: %d-%d of %d",
                 tpl, b + 1, batch_count, m->source_group, start + 1, end, total);

        pipeline_add_node(p, id, prompt);

        for (int j = start; j < end; j++) {
            const char *dep = fanout_expanded_id(src, j);
            if (dep)
                pipeline_add_dep(p, id, dep);
        }
        m->nbatches++;
    }
    return m->nbatches;
}
