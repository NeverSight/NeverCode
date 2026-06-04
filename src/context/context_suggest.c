/*
 * context_suggest.c — Context usage analysis and optimization suggestions
 *
 * Analyzes the conversation's token usage by category (tool results, messages,
 * memory, etc.) and emits actionable suggestions to help users stay within
 * context limits.
 */

#include "nevercode.h"
#include "conversation.h"

void ctx_suggestion_init(ctx_suggestion_t *s) {
    memset(s, 0, sizeof(*s));
}

void ctx_suggestion_free(ctx_suggestion_t *s) {
    free(s->title);
    free(s->detail);
    s->title = NULL;
    s->detail = NULL;
}

void ctx_suggestions_free(ctx_suggestion_t *arr, int n) {
    for (int i = 0; i < n; i++)
        ctx_suggestion_free(&arr[i]);
}

static int add_suggestion(ctx_suggestion_t *out, int idx, int max, ctx_severity_t sev,
                          const char *title, const char *detail, int savings) {
    if (idx >= max)
        return idx;
    out[idx].severity = sev;
    out[idx].title = nc_xstrdup(title);
    out[idx].detail = nc_xstrdup(detail);
    out[idx].savings_tokens = savings;
    return idx + 1;
}

static void check_near_capacity(const ctx_analysis_t *a, ctx_suggestion_t *out, int *n, int max) {
    if (a->usage_pct >= CTX_NEAR_CAPACITY_PCT) {
        char title[NC_TAG_MAX];
        snprintf(title, sizeof(title), "Context is %d%% full", a->usage_pct);
        const char *detail =
            a->autocompact_enabled
                ? "Autocompact will trigger soon. Use /compact now to control what gets kept."
                : "Autocompact is disabled. Use /compact to free space, or enable in /config.";
        *n = add_suggestion(out, *n, max, CTX_SEV_WARNING, title, detail, 0);
    }
}

static void check_tool_result_bloat(const ctx_analysis_t *a, ctx_suggestion_t *out, int *n,
                                    int max) {
    for (int i = 0; i < a->num_tool_stats; i++) {
        int total = a->tool_stats[i].call_tokens + a->tool_stats[i].result_tokens;
        if (a->context_limit <= 0)
            continue;
        int pct = total * 100 / a->context_limit;

        if (pct < CTX_LARGE_TOOL_RESULT_PCT || total < CTX_LARGE_TOOL_RESULT_TOK)
            continue;

        char title[NC_LABEL_MAX], detail[NC_MSG_MAX];
        const char *name = a->tool_stats[i].name;

        if (strcmp(name, "Bash") == 0) {
            snprintf(title, sizeof(title), "Bash results using %d tokens (%d%%)", total, pct);
            snprintf(detail, sizeof(detail),
                     "Pipe output through head, tail, or grep to reduce size. "
                     "Avoid cat on large files — use Read with offset/limit instead.");
            *n = add_suggestion(out, *n, max, CTX_SEV_WARNING, title, detail, total / 2);
        } else if (strcmp(name, "Read") == 0) {
            snprintf(title, sizeof(title), "Read results using %d tokens (%d%%)", total, pct);
            snprintf(detail, sizeof(detail),
                     "Use offset and limit to read only sections you need. "
                     "Avoid re-reading entire files when you only need a few lines.");
            *n = add_suggestion(out, *n, max, CTX_SEV_INFO, title, detail, total * 3 / 10);
        } else if (strcmp(name, "Grep") == 0) {
            snprintf(title, sizeof(title), "Grep results using %d tokens (%d%%)", total, pct);
            snprintf(detail, sizeof(detail),
                     "Add more specific patterns or use the glob parameter to narrow file types.");
            *n = add_suggestion(out, *n, max, CTX_SEV_INFO, title, detail, total * 3 / 10);
        } else if (strcmp(name, "WebFetch") == 0) {
            snprintf(title, sizeof(title), "WebFetch results using %d tokens (%d%%)", total, pct);
            snprintf(detail, sizeof(detail),
                     "Web page content can be very large. Extract only the specific information "
                     "needed.");
            *n = add_suggestion(out, *n, max, CTX_SEV_INFO, title, detail, total * 4 / 10);
        } else if (pct >= 20) {
            snprintf(title, sizeof(title), "%s using %d tokens (%d%%)", name, total, pct);
            snprintf(detail, sizeof(detail),
                     "This tool is consuming a significant portion of context.");
            *n = add_suggestion(out, *n, max, CTX_SEV_INFO, title, detail, total / 5);
        }
    }
}

static void check_memory_bloat(const ctx_analysis_t *a, ctx_suggestion_t *out, int *n, int max) {
    if (a->context_limit <= 0)
        return;
    int pct = a->memory_tokens * 100 / a->context_limit;
    if (pct >= CTX_MEMORY_HIGH_PCT && a->memory_tokens >= CTX_MEMORY_HIGH_TOK) {
        char title[NC_LABEL_MAX];
        snprintf(title, sizeof(title), "Memory files using %d tokens (%d%%)", a->memory_tokens,
                 pct);
        *n = add_suggestion(out, *n, max, CTX_SEV_INFO, title,
                            "Use /memory to review and prune stale entries.",
                            a->memory_tokens * 3 / 10);
    }
}

static void check_autocompact_disabled(const ctx_analysis_t *a, ctx_suggestion_t *out, int *n,
                                       int max) {
    if (!a->autocompact_enabled && a->usage_pct >= 50 && a->usage_pct < CTX_NEAR_CAPACITY_PCT) {
        *n = add_suggestion(out, *n, max, CTX_SEV_INFO, "Autocompact is disabled",
                            "Without autocompact, you will hit context limits. "
                            "Enable it in /config or use /compact manually.",
                            0);
    }
}

static int suggestion_cmp(const void *a, const void *b) {
    const ctx_suggestion_t *sa = a, *sb = b;
    if (sa->severity != sb->severity)
        return sa->severity == CTX_SEV_WARNING ? -1 : 1;
    return sb->savings_tokens - sa->savings_tokens;
}

int ctx_suggest_analyze(const ctx_analysis_t *a, ctx_suggestion_t *out, int max) {
    int n = 0;
    if (max > CTX_MAX_SUGGESTIONS)
        max = CTX_MAX_SUGGESTIONS;

    check_near_capacity(a, out, &n, max);
    check_tool_result_bloat(a, out, &n, max);
    check_memory_bloat(a, out, &n, max);
    check_autocompact_disabled(a, out, &n, max);

    if (n > 1)
        qsort(out, n, sizeof(ctx_suggestion_t), suggestion_cmp);
    return n;
}

void ctx_analysis_from_conversation(ctx_analysis_t *a, const void *conv_ptr, int context_limit,
                                    int memory_tokens, bool autocompact_enabled) {
    const conversation_t *conv = (const conversation_t *)conv_ptr;
    memset(a, 0, sizeof(*a));

    a->context_limit = context_limit;
    a->memory_tokens = memory_tokens;
    a->autocompact_enabled = autocompact_enabled;

    if (!conv || conv->len == 0)
        return;

    int total_tokens = estimate_conversation_tokens(conv->msgs, conv->len, conv->system_prompt);
    a->total_tokens = total_tokens;
    a->usage_pct = context_limit > 0 ? total_tokens * 100 / context_limit : 0;

    /* Gather per-tool token stats */
    typedef struct {
        char name[TOOL_STATS_NAME_MAX];
        int call_tok;
        int result_tok;
    } ts_t;
    ts_t tool_map[CTX_STATS_TOOL_MAX];
    int tm_n = 0;

    for (int i = 0; i < conv->len; i++) {
        const message_t *m = &conv->msgs[i];
        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];
            if (b->type == CONTENT_TOOL_USE && b->tool_name) {
                int est = estimate_tokens(b->text ? b->text : "");
                int found = -1;
                for (int k = 0; k < tm_n; k++) {
                    if (strcmp(tool_map[k].name, b->tool_name) == 0) {
                        found = k;
                        break;
                    }
                }
                if (found < 0 && tm_n < CTX_STATS_TOOL_MAX) {
                    found = tm_n++;
                    nc_strlcpy(tool_map[found].name, b->tool_name, sizeof(tool_map[0].name));
                    tool_map[found].call_tok = 0;
                    tool_map[found].result_tok = 0;
                }
                if (found >= 0)
                    tool_map[found].call_tok += est;
            }
            if (b->type == CONTENT_TOOL_RESULT && b->text) {
                int est = estimate_tokens(b->text);
                const char *tid = b->tool_use_id;
                const char *tname = NULL;
                if (tid) {
                    for (int k = i - 1; k >= 0; k--) {
                        const message_t *pm = &conv->msgs[k];
                        for (int l = 0; l < pm->num_blocks; l++) {
                            if (pm->blocks[l].type == CONTENT_TOOL_USE &&
                                pm->blocks[l].tool_use_id &&
                                strcmp(pm->blocks[l].tool_use_id, tid) == 0) {
                                tname = pm->blocks[l].tool_name;
                                break;
                            }
                        }
                        if (tname)
                            break;
                    }
                }
                if (!tname)
                    tname = "unknown";
                int found = -1;
                for (int k = 0; k < tm_n; k++) {
                    if (strcmp(tool_map[k].name, tname) == 0) {
                        found = k;
                        break;
                    }
                }
                if (found < 0 && tm_n < CTX_STATS_TOOL_MAX) {
                    found = tm_n++;
                    nc_strlcpy(tool_map[found].name, tname, sizeof(tool_map[0].name));
                    tool_map[found].call_tok = 0;
                    tool_map[found].result_tok = 0;
                }
                if (found >= 0)
                    tool_map[found].result_tok += est;
            }
        }
    }

    a->num_tool_stats = tm_n < MAX_CTX_TOOL_STATS ? tm_n : MAX_CTX_TOOL_STATS;
    for (int i = 0; i < a->num_tool_stats; i++) {
        nc_strlcpy(a->tool_stats[i].name, tool_map[i].name, sizeof(a->tool_stats[0].name));
        a->tool_stats[i].call_tokens = tool_map[i].call_tok;
        a->tool_stats[i].result_tokens = tool_map[i].result_tok;
    }
}

char *ctx_suggest_format(const ctx_suggestion_t *suggestions, int n) {
    if (n <= 0)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < n; i++) {
        const char *icon = suggestions[i].severity == CTX_SEV_WARNING ? "!" : "i";
        strbuf_append_str(&sb, "  [");
        strbuf_append_str(&sb, icon);
        strbuf_append_str(&sb, "] ");
        strbuf_append_str(&sb, suggestions[i].title);
        strbuf_append_str(&sb, "\n      ");
        strbuf_append_str(&sb, suggestions[i].detail);
        if (suggestions[i].savings_tokens > 0) {
            char sav[NC_FMT_MAX];
            snprintf(sav, sizeof(sav), " (~%d tokens saveable)", suggestions[i].savings_tokens);
            strbuf_append_str(&sb, sav);
        }
        strbuf_append_str(&sb, "\n");
    }

    return strbuf_detach(&sb);
}
