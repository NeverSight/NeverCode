/*
 * ctx_history.c — Context history management
 *
 * Maintains a sliding window over conversation history to keep the
 * most relevant items within the token budget while preserving
 * critical anchors (system messages, user goals, recent turns).
 */

#include "repl.h"
#include "api.h"

/* CTX_HIST_MAX_ITEMS in defaults.h */
#define CTX_HIST_ANCHOR_SYSTEM 0x01
#define CTX_HIST_ANCHOR_GOAL 0x02
#define CTX_HIST_ANCHOR_RECENT 0x04
#define CTX_HIST_ANCHOR_PINNED 0x08

typedef struct {
    int index;
    int token_est;
    int anchor_flags;
    bool dropped;
    char *summary;
} hist_slot_t;

typedef struct {
    hist_slot_t slots[CTX_HIST_MAX_ITEMS];
    int count;
    int total_tokens;
    int budget;
    int recent_keep;
} ctx_history_t;

static ctx_history_t g_hist;

void ctx_history_init(int budget, int recent_keep) {
    memset(&g_hist, 0, sizeof(g_hist));
    g_hist.budget = budget > 0 ? budget : DEFAULT_CONTEXT_FALLBACK;
    g_hist.recent_keep = recent_keep > 0 ? recent_keep : MICRO_COMPACT_KEEP_RECENT;
}

void ctx_history_clear(void) {
    for (int i = 0; i < g_hist.count; i++)
        free(g_hist.slots[i].summary);
    memset(&g_hist, 0, sizeof(g_hist));
}

int ctx_history_add(int token_est, int anchor_flags) {
    if (g_hist.count >= CTX_HIST_MAX_ITEMS)
        return -1;
    hist_slot_t *s = &g_hist.slots[g_hist.count];
    s->index = g_hist.count;
    s->token_est = token_est;
    s->anchor_flags = anchor_flags;
    s->dropped = false;
    s->summary = NULL;
    g_hist.total_tokens += token_est;
    g_hist.count++;
    return s->index;
}

static bool is_anchored(const hist_slot_t *s) {
    return (s->anchor_flags & (CTX_HIST_ANCHOR_SYSTEM | CTX_HIST_ANCHOR_PINNED)) != 0;
}

static bool is_recent(const hist_slot_t *s, int count, int keep) {
    return s->index >= count - keep;
}

int ctx_history_trim(void) {
    if (g_hist.total_tokens <= g_hist.budget)
        return 0;

    int dropped = 0;
    for (int i = 0; i < g_hist.count && g_hist.total_tokens > g_hist.budget; i++) {
        hist_slot_t *s = &g_hist.slots[i];
        if (s->dropped || is_anchored(s) || is_recent(s, g_hist.count, g_hist.recent_keep))
            continue;
        s->dropped = true;
        g_hist.total_tokens -= s->token_est;
        dropped++;
    }
    return dropped;
}

int ctx_history_trim_to(int target_tokens) {
    if (target_tokens <= 0)
        target_tokens = g_hist.budget;
    int old = g_hist.budget;
    g_hist.budget = target_tokens;
    int d = ctx_history_trim();
    g_hist.budget = old;
    return d;
}

int ctx_history_active_count(void) {
    int n = 0;
    for (int i = 0; i < g_hist.count; i++)
        if (!g_hist.slots[i].dropped)
            n++;
    return n;
}

int ctx_history_total_tokens(void) {
    return g_hist.total_tokens;
}

int ctx_history_count(void) {
    return g_hist.count;
}

bool ctx_history_is_dropped(int index) {
    if (index < 0 || index >= g_hist.count)
        return true;
    return g_hist.slots[index].dropped;
}

int ctx_history_set_summary(int index, const char *summary) {
    if (index < 0 || index >= g_hist.count)
        return -1;
    free(g_hist.slots[index].summary);
    g_hist.slots[index].summary = summary ? nc_xstrdup(summary) : NULL;
    return 0;
}

const char *ctx_history_get_summary(int index) {
    if (index < 0 || index >= g_hist.count)
        return NULL;
    return g_hist.slots[index].summary;
}

int ctx_history_pin(int index) {
    if (index < 0 || index >= g_hist.count)
        return -1;
    g_hist.slots[index].anchor_flags |= CTX_HIST_ANCHOR_PINNED;
    if (g_hist.slots[index].dropped) {
        g_hist.slots[index].dropped = false;
        g_hist.total_tokens += g_hist.slots[index].token_est;
    }
    return 0;
}

int ctx_history_unpin(int index) {
    if (index < 0 || index >= g_hist.count)
        return -1;
    g_hist.slots[index].anchor_flags &= ~CTX_HIST_ANCHOR_PINNED;
    return 0;
}

char *ctx_history_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    int active = ctx_history_active_count();
    strbuf_appendf(&sb, "History: %d/%d items active, %d tokens (budget %d)\n", active,
                   g_hist.count, g_hist.total_tokens, g_hist.budget);
    return strbuf_detach(&sb);
}
