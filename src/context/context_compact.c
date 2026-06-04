/*
 * context_compact.c — Four-level progressive context compaction.
 *
 * Level 1: Snip — zero-cost marker replacement for stale tool results.
 * Level 2: MicroCompact — cache-aware bulk tool result clearance.
 * Level 3: Collapse — selective message group folding (restructure
 *          without LLM; preserves more detail than full compaction).
 * Level 4: AutoCompact — full LLM-based conversation summary (see
 *          context_manager.c).
 *
 * The four levels are invoked in order of ascending cost; each level
 * is only triggered when the preceding levels cannot free enough space.
 */

#include "nevercode.h"
#include "conversation.h"

#define SNIP_MARKER COMPACT_SNIP_MARKER
#define MICRO_KEEP_RECENT MICRO_COMPACT_KEEP_RECENT

static const char *compactable_tools[] = {"Read",      "Bash",     "Grep", "Glob",
                                          "WebSearch", "WebFetch", "Edit", "Write",
                                          "ListDir",   "Agent",    NULL};

static const char *prune_protected_tools[] = {"Skill",  "Config", "MCP",  "ToolSuggest",
                                              "Memory", "Brief",  "Cron", NULL};

static bool is_prune_protected(const char *name) {
    if (!name)
        return false;
    for (int i = 0; prune_protected_tools[i]; i++) {
        if (strcmp(prune_protected_tools[i], name) == 0)
            return true;
    }
    return false;
}

static bool is_compactable_tool(const char *name) {
    if (!name)
        return false;
    if (is_prune_protected(name))
        return false;
    for (int i = 0; compactable_tools[i]; i++) {
        if (strcmp(compactable_tools[i], name) == 0)
            return true;
    }
    return false;
}

/* ── Level 1: Snip ── */

int context_snip_message(message_t *m) {
    if (!m || m->role != ROLE_TOOL)
        return 0;
    int freed = 0;
    for (int i = 0; i < m->num_blocks; i++) {
        content_block_t *b = &m->blocks[i];
        if (b->type == CONTENT_TOOL_RESULT && b->text && strcmp(b->text, SNIP_MARKER) != 0) {
            freed += (int)strlen(b->text);
            free(b->text);
            b->text = nc_xstrdup(SNIP_MARKER);
        }
    }
    return freed;
}

int context_snip_range(conversation_t *conv, int from, int to) {
    if (!conv || from < 0)
        return 0;
    if (to > conv->len)
        to = conv->len;
    int total_freed = 0;
    for (int i = from; i < to; i++) {
        total_freed += context_snip_message(&conv->msgs[i]);
    }
    return total_freed;
}

/* ── Level 2: MicroCompact ── */

int context_micro_compact(conversation_t *conv, time_t last_assistant_time, int cache_timeout_sec) {
    if (!conv || conv->len < 4)
        return 0;

    time_t now = nc_time_now();
    if (cache_timeout_sec <= 0)
        cache_timeout_sec = MICRO_COMPACT_CACHE_TTL;

    if (last_assistant_time > 0 && (now - last_assistant_time) < cache_timeout_sec)
        return 0;

    int tool_result_indices[CTX_MAX_TOOL_RESULT_IDX];
    int num_tool_results = 0;

    for (int i = 0; i < conv->len && num_tool_results < CTX_MAX_TOOL_RESULT_IDX; i++) {
        message_t *m = &conv->msgs[i];
        if (m->role != ROLE_TOOL)
            continue;
        for (int j = 0; j < m->num_blocks; j++) {
            content_block_t *b = &m->blocks[j];
            if (b->type == CONTENT_TOOL_RESULT && b->text && strcmp(b->text, SNIP_MARKER) != 0 &&
                b->tool_name && is_compactable_tool(b->tool_name)) {
                tool_result_indices[num_tool_results++] = i;
                break;
            }
        }
    }

    if (num_tool_results <= MICRO_KEEP_RECENT)
        return 0;

    int to_clear = num_tool_results - MICRO_KEEP_RECENT;
    int total_freed = 0;
    for (int k = 0; k < to_clear; k++) {
        int idx = tool_result_indices[k];
        total_freed += context_snip_message(&conv->msgs[idx]);
    }
    return total_freed;
}

/* ── Level 3: Collapse ── */

/*
 * Collapse folds consecutive assistant+tool round-trips into compact
 * one-line summaries while keeping the message structure intact.
 * Unlike auto-compact, it does NOT call the LLM — it uses heuristics
 * to condense tool round-trips that are older than `keep_recent`
 * rounds into single-line "[tool_name → exit_code: first_line]" form.
 *
 * Returns the number of characters freed.
 */
int context_collapse(conversation_t *conv, int keep_recent_rounds) {
    if (!conv || conv->len < 6)
        return 0;
    if (keep_recent_rounds < COLLAPSE_KEEP_RECENT)
        keep_recent_rounds = COLLAPSE_KEEP_RECENT;

    msg_group_t groups[CTX_MAX_MSG_GROUPS];
    int ng = compact_group_by_api_round(conv->msgs, conv->len, groups, CTX_MAX_MSG_GROUPS);
    if (ng <= keep_recent_rounds)
        return 0;

    int foldable = ng - keep_recent_rounds;
    int total_freed = 0;

    for (int g = 0; g < foldable; g++) {
        for (int i = groups[g].start; i < groups[g].end; i++) {
            message_t *m = &conv->msgs[i];

            if (m->role == ROLE_ASSISTANT) {
                for (int j = 0; j < m->num_blocks; j++) {
                    content_block_t *b = &m->blocks[j];
                    if (b->type == CONTENT_TEXT && b->text &&
                        strlen(b->text) > COLLAPSE_TEXT_THRESHOLD) {
                        int old_len = (int)strlen(b->text);
                        char *truncated = middle_truncate_bytes(b->text, COLLAPSE_SUMMARY_BYTES);
                        if (truncated) {
                            free(b->text);
                            b->text = truncated;
                            total_freed += old_len - (int)strlen(truncated);
                        }
                    }
                }
            }

            if (m->role == ROLE_TOOL) {
                total_freed += context_snip_message(m);
            }
        }
    }

    return total_freed;
}

bool context_collapse_needed(int est_tokens, int limit) {
    if (limit <= 0)
        return false;
    return (int)((long long)est_tokens * 100 / limit) >= COLLAPSE_TRIGGER_PCT;
}

/* ── Time-based Pre-call MicroCompact ── */

/*
 * Proactive microcompact before an API call when enough idle time has
 * elapsed that the provider's prompt cache has expired.  Running before
 * the request shrinks what gets rewritten to the server, saving latency
 * and cost for the first post-idle turn.
 */

static time_t g_last_assistant_ts = 0;

void context_compact_touch_assistant(void) {
    g_last_assistant_ts = nc_time_now();
}

time_t context_compact_last_assistant(void) {
    return g_last_assistant_ts;
}

int context_compact_pre_call(void *conv_ptr, int gap_threshold_min, int keep_recent) {
    conversation_t *conv = (conversation_t *)conv_ptr;
    if (!conv || gap_threshold_min <= 0)
        return 0;
    if (g_last_assistant_ts == 0)
        return 0;

    time_t now = nc_time_now();
    long gap_min = (now - g_last_assistant_ts) / 60;
    if (gap_min < gap_threshold_min)
        return 0;

    int freed = context_micro_compact(conv, 0, 1);
    return freed;
}

/* ── Mid-Turn Compaction Injection ── */

/*
 * Insert initial context (system prompt summary, goals, recent decisions)
 * into the compaction result at the appropriate position based on the
 * injection mode.
 *
 * COMPACT_INJECT_NONE: no injection; caller will reinject after compaction.
 * COMPACT_INJECT_BEFORE_LAST_USER: insert just before the last real user
 *     message so the model sees compacted context in the expected position.
 */
int context_inject_initial(conversation_t *conv, compact_inject_mode_t mode,
                           const char *initial_ctx) {
    if (!conv || !initial_ctx || mode == COMPACT_INJECT_NONE)
        return 0;

    int last_user = -1;
    for (int i = conv->len - 1; i >= 0; i--) {
        if (conv->msgs[i].role == ROLE_USER) {
            last_user = i;
            break;
        }
    }

    if (last_user < 0 || last_user >= conv->len)
        return -1;

    message_t *injected = conversation_insert_at(conv, last_user, ROLE_USER);
    if (!injected)
        return -1;
    message_add_text(injected, initial_ctx);
    return 0;
}

/* ── Level 1.5: Tool Output Prune (cheap pre-pass before LLM summary) ── */

int context_prune_tool_outputs(conversation_t *conv, int keep_recent_rounds) {
    if (!conv || conv->len < 4)
        return 0;
    if (keep_recent_rounds < 2)
        keep_recent_rounds = 2;

    msg_group_t groups[CTX_MAX_MSG_GROUPS];
    int ng = compact_group_by_api_round(conv->msgs, conv->len, groups, CTX_MAX_MSG_GROUPS);
    if (ng <= keep_recent_rounds)
        return 0;

    int prunable = ng - keep_recent_rounds;
    int total_freed = 0;

    for (int g = 0; g < prunable; g++) {
        for (int i = groups[g].start; i < groups[g].end; i++) {
            message_t *m = &conv->msgs[i];
            if (m->role != ROLE_TOOL)
                continue;
            for (int j = 0; j < m->num_blocks; j++) {
                content_block_t *b = &m->blocks[j];
                if (b->type != CONTENT_TOOL_RESULT || !b->text)
                    continue;
                if (strcmp(b->text, SNIP_MARKER) == 0)
                    continue;
                if (strcmp(b->text, COMPACT_PRUNED_TOOL_PLACEHOLDER) == 0)
                    continue;
                if (is_prune_protected(b->tool_name))
                    continue;
                int old = (int)strlen(b->text);
                free(b->text);
                b->text = nc_xstrdup(COMPACT_PRUNED_TOOL_PLACEHOLDER);
                total_freed += old - (int)strlen(COMPACT_PRUNED_TOOL_PLACEHOLDER);
            }
        }
    }
    return total_freed;
}

/* ── Compression Metrics ── */

typedef struct {
    int chars_freed;
    int tokens_saved;
    int level; /* 1=snip, 2=micro, 3=collapse, 4=auto */
    long timestamp;
} compact_metric_t;

static compact_metric_t g_metric_ring[COMPACT_METRIC_HISTORY];
static int g_metric_head;
static int g_metric_count;
static long g_metric_total_chars;
static long g_metric_total_tokens;
static int g_metric_invocations;

void compact_metrics_record(int level, int chars_freed) {
    compact_metric_t *e = &g_metric_ring[g_metric_head];
    e->chars_freed = chars_freed;
    e->tokens_saved = chars_freed / APPROX_BYTES_PER_TOKEN;
    e->level = level;
    e->timestamp = nc_wall_sec();

    g_metric_head = (g_metric_head + 1) % COMPACT_METRIC_HISTORY;
    if (g_metric_count < COMPACT_METRIC_HISTORY)
        g_metric_count++;
    g_metric_total_chars += chars_freed;
    g_metric_total_tokens += e->tokens_saved;
    g_metric_invocations++;
}

char *compact_metrics_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Compactions: %d | chars freed: %ld | tokens saved: ~%ld",
                   g_metric_invocations, g_metric_total_chars, g_metric_total_tokens);
    if (g_metric_count > 0) {
        int idx = (g_metric_head - 1 + COMPACT_METRIC_HISTORY) % COMPACT_METRIC_HISTORY;
        compact_metric_t *last = &g_metric_ring[idx];
        strbuf_appendf(&sb, " | last: L%d %d chars", last->level, last->chars_freed);
    }
    return strbuf_detach(&sb);
}

void compact_metrics_reset(void) {
    memset(g_metric_ring, 0, sizeof(g_metric_ring));
    g_metric_head = g_metric_count = g_metric_invocations = 0;
    g_metric_total_chars = g_metric_total_tokens = 0;
}

int compact_metrics_total_invocations(void) {
    return g_metric_invocations;
}

long compact_metrics_total_tokens_saved(void) {
    return g_metric_total_tokens;
}

/* ── Shared Utilities ── */

int context_estimate_freed_tokens(int freed_chars) {
    return freed_chars / APPROX_BYTES_PER_TOKEN;
}
