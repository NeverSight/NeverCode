/*
 * auto_compact.c — Progressive 4-level context compaction pipeline.
 *
 * Tracks effective context window (model window minus output reserve)
 * and triggers compaction when usage crosses a threshold.
 *
 * Levels (each triggered only when the previous cannot free enough):
 *   L1 Snip:         Zero-cost marker replacement for stale tool results.
 *   L2 MicroCompact: Bulk image/attachment stripping.
 *   L3 Collapse:     Selective message truncation (oldest rounds).
 *   L4 Full:         LLM-based summarization (delegated to caller).
 *
 * A circuit breaker prevents runaway compaction after repeated failures.
 */

#include "nevercode.h"
#include "conversation.h"

typedef struct {
    int model_context_window;
    int output_reserve;
    int effective_window;
    int compact_trigger_pct;
    int warning_pct;
    int total_compactions;
    int snip_count;
    int micro_count;
    int collapse_count;
    int full_count;
    long last_compact_time;
    int last_compact_turn;
    int circuit_trips;
    int consecutive_failures;
    long circuit_cooldown_until;
    bool enabled;
} auto_compact_ctx_t;

static auto_compact_ctx_t g_ac;

/* ── Lifecycle ── */

static int compute_reserve(int max_out) {
    int r = max_out > 0 ? max_out : DEFAULT_MAX_TOKENS;
    return r < AUTO_COMPACT_RESERVE_TOKENS ? r : AUTO_COMPACT_RESERVE_TOKENS;
}

void auto_compact_init(int mcw) {
    memset(&g_ac, 0, sizeof(g_ac));
    g_ac.model_context_window = mcw > 0 ? mcw : DEFAULT_CONTEXT_FALLBACK;
    g_ac.output_reserve = compute_reserve(DEFAULT_MAX_TOKENS);
    g_ac.effective_window = g_ac.model_context_window - g_ac.output_reserve;
    if (g_ac.effective_window < AUTO_COMPACT_MIN_WINDOW)
        g_ac.effective_window = AUTO_COMPACT_MIN_WINDOW;
    g_ac.compact_trigger_pct = CTX_ZONE_COMPACT_PCT;
    g_ac.warning_pct = CTX_ZONE_WARNING_PCT;
    g_ac.enabled = true;
}

void auto_compact_free(void) {
    memset(&g_ac, 0, sizeof(g_ac));
}

/* ── Accessors ── */

void auto_compact_set_enabled(bool on) {
    g_ac.enabled = on;
}
bool auto_compact_enabled(void) {
    return g_ac.enabled;
}
int auto_compact_effective_window(void) {
    return g_ac.effective_window;
}
int auto_compact_total(void) {
    return g_ac.total_compactions;
}

void auto_compact_set_window(int cw, int ov) {
    g_ac.model_context_window = cw > 0 ? cw : DEFAULT_CONTEXT_FALLBACK;
    g_ac.output_reserve = compute_reserve(ov > 0 ? ov : DEFAULT_MAX_TOKENS);
    g_ac.effective_window = g_ac.model_context_window - g_ac.output_reserve;
    if (g_ac.effective_window < AUTO_COMPACT_MIN_WINDOW)
        g_ac.effective_window = AUTO_COMPACT_MIN_WINDOW;
}

/* ── Metrics ── */

int auto_compact_usage_pct(int ct) {
    if (g_ac.effective_window <= 0)
        return 0;
    return (int)((long)ct * 100 / g_ac.effective_window);
}

bool auto_compact_should_trigger(int ct) {
    if (!g_ac.enabled)
        return false;
    if (g_ac.consecutive_failures >= CIRCUIT_BREAKER_THRESHOLD &&
        nc_wall_sec() < g_ac.circuit_cooldown_until)
        return false;
    return auto_compact_usage_pct(ct) >= g_ac.compact_trigger_pct;
}

/* ── 4-Level Compaction Pipeline ── */

int auto_compact_run(void *conv_ptr, int ct, const char *sid) {
    if (!g_ac.enabled || !conv_ptr)
        return 0;

    conversation_t *conv = (conversation_t *)conv_ptr;
    int freed = 0;

    /* L1: Snip stale tool results */
    for (int i = 0; i < conv->len; i++) {
        int s = context_snip_message(&conv->msgs[i]);
        if (s > 0)
            freed += context_estimate_freed_tokens(s);
    }
    if (freed > 0)
        g_ac.snip_count++;
    if (auto_compact_usage_pct(ct - freed) < g_ac.compact_trigger_pct)
        goto done;

    /* L2a: Strip images and large attachments */
    {
        int mc = compact_strip_images(conv->msgs, conv->len);
        if (mc > 0) {
            freed += context_estimate_freed_tokens(mc);
            g_ac.micro_count++;
        }
        if (auto_compact_usage_pct(ct - freed) < g_ac.compact_trigger_pct)
            goto done;
    }

    /* L2b: Prune <thinking> blocks from old assistant messages */
    {
        int tp = compact_prune_thinking(conv->msgs, conv->len, COMPACT_PRUNE_THINKING_KEEP_RECENT);
        if (tp > 0)
            freed += tp * (APPROX_BYTES_PER_TOKEN * COMPACT_THINKING_PRUNE_FACTOR);
        if (auto_compact_usage_pct(ct - freed) < g_ac.compact_trigger_pct)
            goto done;
    }

    /* L3: Collapse oldest message rounds */
    {
        int rem = thread_truncate_safe(conv->msgs, conv->len, 2);
        if (rem > 0) {
            conv->len -= rem;
            freed += rem * COMPACT_COLLAPSE_EST_TOKENS;
            g_ac.collapse_count++;
        }
        if (auto_compact_usage_pct(ct - freed) < g_ac.compact_trigger_pct)
            goto done;
    }

    /* L4: Full compaction (caller handles LLM summarization) */
    g_ac.full_count++;

done:
    g_ac.consecutive_failures = 0;
    g_ac.total_compactions++;
    g_ac.last_compact_time = nc_wall_sec();
    if (sid)
        g_ac.last_compact_turn = state_session_turn_number();
    post_compact_run(false);
    return freed;
}

/* ── Overflow detection ── */

void auto_compact_set_input_limit(int lim) {
    if (lim > 0 && lim < g_ac.effective_window)
        g_ac.effective_window = lim;
}

bool auto_compact_is_overflow(int token_count, int output_tokens) {
    if (!g_ac.enabled)
        return false;

    int reserved = compute_reserve(output_tokens > 0 ? output_tokens : DEFAULT_MAX_TOKENS);
    int usable = g_ac.model_context_window - reserved;
    if (g_ac.effective_window > 0 && g_ac.effective_window < usable)
        usable = g_ac.effective_window;
    if (usable <= 0)
        usable = AUTO_COMPACT_MIN_WINDOW;

    return token_count >= usable;
}

/* ── Circuit breaker ── */

void auto_compact_record_failure(void) {
    g_ac.consecutive_failures++;
    if (g_ac.consecutive_failures >= CIRCUIT_BREAKER_THRESHOLD) {
        g_ac.circuit_trips++;
        g_ac.circuit_cooldown_until = nc_wall_sec() + CIRCUIT_BREAKER_COOLDOWN_S;
    }
}

void auto_compact_reset_circuit(void) {
    g_ac.consecutive_failures = 0;
    g_ac.circuit_cooldown_until = 0;
}

int auto_compact_circuit_trips(void) {
    return g_ac.circuit_trips;
}

/* ── Degradation check ── */

static bool g_degrade_warned = false;

bool auto_compact_should_warn_degradation(void) {
    if (g_degrade_warned)
        return false;
    if (g_ac.full_count >= AUTO_COMPACT_DEGRADE_WARN_THRESHOLD) {
        g_degrade_warned = true;
        return true;
    }
    return false;
}

int auto_compact_full_count(void) {
    return g_ac.full_count;
}

/* ── Diagnostics ── */

char *auto_compact_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_appendf(&sb,
                   "Auto-Compact: %s, window=%d (model=%d - reserve=%d)\n"
                   "  Compactions: %d total (snip=%d, micro=%d, collapse=%d, full=%d)\n"
                   "  Circuit breaker: %d trips, %d consecutive failures\n",
                   g_ac.enabled ? "on" : "off", g_ac.effective_window, g_ac.model_context_window,
                   g_ac.output_reserve, g_ac.total_compactions, g_ac.snip_count, g_ac.micro_count,
                   g_ac.collapse_count, g_ac.full_count, g_ac.circuit_trips,
                   g_ac.consecutive_failures);

    if (g_ac.last_compact_time > 0) {
        long ago = nc_wall_sec() - g_ac.last_compact_time;
        strbuf_appendf(&sb, "  Last compact: %lds ago (turn %d)\n", ago, g_ac.last_compact_turn);
    }

    return strbuf_detach(&sb);
}
