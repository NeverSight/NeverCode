/*
 * ctx_viz.c — Context window visualization tool
 *
 * Provides a visual breakdown of how the context window
 * is utilized: system prompt, messages, tool results, etc.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Build from conversation ── */

int ctx_viz_analyze(ctx_viz_result_t *viz, const message_t *msgs, int msg_count, int context_window,
                    int system_prompt_tokens) {
    if (!viz)
        return -1;
    memset(viz, 0, sizeof(*viz));
    viz->window_size = context_window > 0 ? context_window : DEFAULT_CONTEXT_FALLBACK;

    /* System prompt */
    if (system_prompt_tokens > 0) {
        ctx_segment_t *s = &viz->segments[viz->count++];
        s->label = "system_prompt";
        s->tokens = system_prompt_tokens;
        viz->total_tokens += system_prompt_tokens;
    }

    int user_tokens = 0, assistant_tokens = 0, tool_tokens = 0;

    for (int i = 0; i < msg_count; i++) {
        int msg_est = 0;
        for (int b = 0; b < msgs[i].num_blocks; b++) {
            const content_block_t *blk = &msgs[i].blocks[b];
            if (blk->text)
                msg_est += (int)strlen(blk->text) / 4;
            if (blk->tool_input) {
                char *s = cJSON_PrintUnformatted(blk->tool_input);
                if (s) {
                    msg_est += (int)strlen(s) / 4;
                    free(s);
                }
            }
        }

        switch (msgs[i].role) {
        case ROLE_USER:
            user_tokens += msg_est;
            break;
        case ROLE_ASSISTANT:
            assistant_tokens += msg_est;
            break;
        case ROLE_TOOL:
            tool_tokens += msg_est;
            break;
        case ROLE_SYSTEM:
            break;
        }
    }

    if (user_tokens > 0) {
        viz->segments[viz->count++] = (ctx_segment_t){"user_messages", user_tokens, 0};
        viz->total_tokens += user_tokens;
    }
    if (assistant_tokens > 0) {
        viz->segments[viz->count++] = (ctx_segment_t){"assistant_responses", assistant_tokens, 0};
        viz->total_tokens += assistant_tokens;
    }
    if (tool_tokens > 0) {
        viz->segments[viz->count++] = (ctx_segment_t){"tool_results", tool_tokens, 0};
        viz->total_tokens += tool_tokens;
    }

    viz->free_tokens = viz->window_size - viz->total_tokens;
    if (viz->free_tokens < 0)
        viz->free_tokens = 0;

    /* Compute percentages */
    for (int i = 0; i < viz->count; i++) {
        if (viz->window_size > 0)
            viz->segments[i].pct = viz->segments[i].tokens * 100 / viz->window_size;
    }

    return 0;
}

/* ── Rendering ── */

static void render_bar(char *out, int sz, int pct, int width) {
    int filled = pct * width / 100;
    if (filled > width)
        filled = width;
    int pos = 0;
    out[pos++] = '[';
    for (int i = 0; i < width && pos < sz - 2; i++)
        out[pos++] = (i < filled) ? '#' : '.';
    out[pos++] = ']';
    out[pos] = '\0';
}

char *ctx_viz_render(const ctx_viz_result_t *viz) {
    if (!viz)
        return NULL;
    size_t sz = NC_ERRBUF_MAX;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    int used_pct = viz->window_size > 0 ? viz->total_tokens * 100 / viz->window_size : 0;
    char bar[NC_FMT_MAX];
    render_bar(bar, sizeof(bar), used_pct, 40);

    int off = snprintf(buf, sz, "Context Window: %d / %d tokens (%d%% used)\n%s\n\n",
                       viz->total_tokens, viz->window_size, used_pct, bar);

    for (int i = 0; i < viz->count; i++) {
        char seg_bar[NC_SHORT_MAX];
        render_bar(seg_bar, sizeof(seg_bar), viz->segments[i].pct, 20);
        off += snprintf(buf + off, sz - (size_t)off, "  %-24s %6d tok (%2d%%) %s\n",
                        viz->segments[i].label, viz->segments[i].tokens, viz->segments[i].pct,
                        seg_bar);
    }

    off += snprintf(buf + off, sz - (size_t)off, "\n  Free space: %d tokens (%d%%)\n",
                    viz->free_tokens,
                    viz->window_size > 0 ? viz->free_tokens * 100 / viz->window_size : 0);

    if (used_pct >= CTX_ZONE_BLOCKING_PCT)
        off += snprintf(buf + off, sz - (size_t)off, "  ⚠ CRITICAL: context nearly full\n");
    else if (used_pct >= CTX_ZONE_COMPACT_PCT)
        off += snprintf(buf + off, sz - (size_t)off, "  ⚠ WARNING: compaction recommended\n");

    return buf;
}

char *ctx_viz_render_json(const ctx_viz_result_t *viz) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total_tokens", viz->total_tokens);
    cJSON_AddNumberToObject(root, "window_size", viz->window_size);
    cJSON_AddNumberToObject(root, "free_tokens", viz->free_tokens);
    cJSON_AddNumberToObject(root, "used_pct",
                            viz->window_size > 0 ? viz->total_tokens * 100 / viz->window_size : 0);

    cJSON *segs = cJSON_CreateArray();
    for (int i = 0; i < viz->count; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "label", viz->segments[i].label);
        cJSON_AddNumberToObject(s, "tokens", viz->segments[i].tokens);
        cJSON_AddNumberToObject(s, "pct", viz->segments[i].pct);
        cJSON_AddItemToArray(segs, s);
    }
    cJSON_AddItemToObject(root, "segments", segs);

    char *out = nc_cjson_detach(root);
    return out;
}
