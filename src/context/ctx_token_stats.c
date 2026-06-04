/*
 * ctx_token_stats.c — Detailed per-category token distribution analysis.
 *
 * Breaks down a conversation's token footprint by message type (human,
 * assistant, tool request, tool result) and identifies duplicate file
 * reads that waste context budget.
 */

#include "nevercode.h"

/* Internal tracking for file reads */
typedef struct {
    char path[NC_PATH_MAX];
    int count;
} read_track_t;

void ctx_token_stats_init(ctx_token_stats_t *s) {
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
}

static int block_tokens(const content_block_t *b) {
    int total = 0;
    if (b->text)
        total += estimate_tokens(b->text);
    if (b->tool_name)
        total += estimate_tokens(b->tool_name);
    if (b->tool_input) {
        char *js = cJSON_PrintUnformatted(b->tool_input);
        if (js) {
            total += estimate_tokens(js);
            free(js);
        }
    }
    return total;
}

static void record_tool(ctx_token_stats_t *s, const char *name, int req_delta, int res_tok) {
    if (!name)
        return;
    for (int t = 0; t < s->tool_type_count; t++) {
        if (strcmp(s->tool_types[t].name, name) == 0) {
            s->tool_types[t].request_count += req_delta;
            s->tool_types[t].result_tokens += res_tok;
            return;
        }
    }
    if (s->tool_type_count < CTX_STATS_TOOL_MAX) {
        int ti = s->tool_type_count++;
        nc_strlcpy(s->tool_types[ti].name, name, sizeof(s->tool_types[ti].name));
        s->tool_types[ti].request_count = req_delta;
        s->tool_types[ti].result_tokens = res_tok;
    }
}

void ctx_token_stats_analyze(ctx_token_stats_t *s, const message_t *msgs, int len) {
    if (!s)
        return;
    ctx_token_stats_init(s);
    if (!msgs || len <= 0)
        return;

    read_track_t reads[CTX_STATS_DUP_READ_MAX];
    int nreads = 0;

    for (int i = 0; i < len; i++) {
        const message_t *m = &msgs[i];
        for (int b = 0; b < m->num_blocks; b++) {
            const content_block_t *blk = &m->blocks[b];
            int tok = block_tokens(blk);

            if (m->role == ROLE_SYSTEM) {
                s->system_tokens += tok;
            } else if (m->role == ROLE_USER) {
                if (blk->type == CONTENT_TOOL_RESULT) {
                    s->tool_result_tokens += tok;
                    s->tool_result_count++;
                    record_tool(s, blk->tool_name, 0, tok);
                } else {
                    s->user_tokens += tok;
                }
            } else if (m->role == ROLE_ASSISTANT) {
                if (blk->type == CONTENT_TOOL_USE) {
                    s->tool_request_tokens += tok;
                    s->tool_request_count++;
                    record_tool(s, blk->tool_name, 1, 0);

                    /* Track file reads for duplicate detection */
                    if (blk->tool_name && (strcmp(blk->tool_name, "Read") == 0 ||
                                           strcmp(blk->tool_name, "FileRead") == 0)) {
                        const char *path = NULL;
                        if (blk->tool_input) {
                            cJSON *pj = cJSON_GetObjectItem(blk->tool_input, "path");
                            if (pj)
                                path = cJSON_GetStringValue(pj);
                        }
                        if (path) {
                            bool found = false;
                            for (int r = 0; r < nreads; r++) {
                                if (strcmp(reads[r].path, path) == 0) {
                                    reads[r].count++;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found && nreads < CTX_STATS_DUP_READ_MAX) {
                                nc_strlcpy(reads[nreads].path, path, sizeof(reads[nreads].path));
                                reads[nreads].count = 1;
                                nreads++;
                            }
                        }
                    }
                } else {
                    s->assistant_tokens += tok;
                }
            }
        }

        /* Count messages (not blocks) */
        if (m->role == ROLE_USER) {
            bool has_tool_result = false;
            for (int b = 0; b < m->num_blocks; b++) {
                if (m->blocks[b].type == CONTENT_TOOL_RESULT) {
                    has_tool_result = true;
                    break;
                }
            }
            if (!has_tool_result)
                s->user_count++;
        } else if (m->role == ROLE_ASSISTANT) {
            s->assistant_count++;
        }
    }

    /* Collect duplicates (count >= 2) */
    s->dup_read_count = 0;
    for (int r = 0; r < nreads && s->dup_read_count < CTX_STATS_DUP_READ_MAX; r++) {
        if (reads[r].count >= 2) {
            nc_strlcpy(s->dup_reads[s->dup_read_count].path, reads[r].path,
                       sizeof(s->dup_reads[s->dup_read_count].path));
            s->dup_reads[s->dup_read_count].count = reads[r].count;
            s->dup_reads[s->dup_read_count].total_tokens = 0;
            s->dup_read_count++;
        }
    }

    s->total_tokens = s->system_tokens + s->user_tokens + s->assistant_tokens +
                      s->tool_request_tokens + s->tool_result_tokens;
}

char *ctx_token_stats_format(const ctx_token_stats_t *s) {
    if (!s)
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_appendf(&sb, "Token distribution (%d total):\n", s->total_tokens);
    if (s->total_tokens == 0) {
        strbuf_append_str(&sb, "  (empty conversation)\n");
        return strbuf_detach(&sb);
    }

    int pct_u = (s->user_tokens * 100) / s->total_tokens;
    int pct_a = (s->assistant_tokens * 100) / s->total_tokens;
    int pct_tr = (s->tool_result_tokens * 100) / s->total_tokens;
    int pct_tq = (s->tool_request_tokens * 100) / s->total_tokens;
    int pct_s = (s->system_tokens * 100) / s->total_tokens;

    strbuf_appendf(&sb, "  System:       %6d (%2d%%)\n", s->system_tokens, pct_s);
    strbuf_appendf(&sb, "  User:         %6d (%2d%%)  [%d msgs]\n", s->user_tokens, pct_u,
                   s->user_count);
    strbuf_appendf(&sb, "  Assistant:    %6d (%2d%%)  [%d msgs]\n", s->assistant_tokens, pct_a,
                   s->assistant_count);
    strbuf_appendf(&sb, "  Tool calls:   %6d (%2d%%)  [%d calls]\n", s->tool_request_tokens, pct_tq,
                   s->tool_request_count);
    strbuf_appendf(&sb, "  Tool results: %6d (%2d%%)  [%d results]\n", s->tool_result_tokens,
                   pct_tr, s->tool_result_count);

    if (s->tool_type_count > 0) {
        strbuf_append_str(&sb, "\n  By tool:\n");
        for (int t = 0; t < s->tool_type_count; t++) {
            const ctx_tool_type_stat_t *ts = &s->tool_types[t];
            strbuf_appendf(&sb, "    %-20s  calls=%d  result_tok=%d\n", ts->name, ts->request_count,
                           ts->result_tokens);
        }
    }

    if (s->dup_read_count > 0) {
        strbuf_append_str(&sb, "\n  Duplicate file reads (wasting context):\n");
        for (int d = 0; d < s->dup_read_count; d++) {
            strbuf_appendf(&sb, "    %s  (%dx)\n", s->dup_reads[d].path, s->dup_reads[d].count);
        }
    }

    return strbuf_detach(&sb);
}
