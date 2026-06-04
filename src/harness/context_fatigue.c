/*
 * context_fatigue.c — Proactive context exhaustion prediction
 *
 * Tracks token consumption velocity across turns to predict when
 * the context window will become exhausted.  Triggers pre-emptive
 * compaction before the cliff edge (see CTX_ZONE_* / harness thresholds
 * in defaults.h) where output quality degrades sharply.
 *
 * Key insight: context degradation is a cliff, not a slope.
 * Waiting until the model fails is too late — we must act earlier.
 */

#include "harness.h"
#include <math.h>

/* FATIGUE_SAMPLE_MAX, FATIGUE_EMA_ALPHA in defaults.h */

typedef struct {
    int turn;
    int tokens_used;
    int tokens_added;
    long timestamp;
} fatigue_sample_t;

typedef struct {
    fatigue_sample_t samples[FATIGUE_SAMPLE_MAX];
    int count;
    int context_limit;
    double ema_velocity;
    int last_total;
    bool initialized;
} fatigue_ctx_t;

static fatigue_ctx_t g_fatigue;

/* ── Init / Free ── */

void context_fatigue_init(int context_window_tokens) {
    memset(&g_fatigue, 0, sizeof(g_fatigue));
    g_fatigue.context_limit =
        context_window_tokens > 0 ? context_window_tokens : DEFAULT_CONTEXT_FALLBACK;
}

void context_fatigue_free(void) {
    memset(&g_fatigue, 0, sizeof(g_fatigue));
}

/* ── Recording ── */

void context_fatigue_record(int turn, int total_tokens) {
    int delta = total_tokens - g_fatigue.last_total;
    if (delta < 0)
        delta = 0;

    /* Sliding window eviction */
    if (g_fatigue.count >= FATIGUE_SAMPLE_MAX) {
        memmove(&g_fatigue.samples[0], &g_fatigue.samples[1],
                (FATIGUE_SAMPLE_MAX - 1) * sizeof(fatigue_sample_t));
        g_fatigue.count = FATIGUE_SAMPLE_MAX - 1;
    }

    fatigue_sample_t *s = &g_fatigue.samples[g_fatigue.count++];
    s->turn = turn;
    s->tokens_used = total_tokens;
    s->tokens_added = delta;
    s->timestamp = nc_wall_sec();

    /* EMA velocity update */
    if (!g_fatigue.initialized) {
        g_fatigue.ema_velocity = (double)delta;
        g_fatigue.initialized = true;
    } else {
        g_fatigue.ema_velocity =
            FATIGUE_EMA_ALPHA * delta + (1.0 - FATIGUE_EMA_ALPHA) * g_fatigue.ema_velocity;
    }

    g_fatigue.last_total = total_tokens;
}

/* ── Queries ── */

int context_fatigue_pct(void) {
    if (g_fatigue.context_limit <= 0)
        return 0;
    return (int)((long)g_fatigue.last_total * 100 / g_fatigue.context_limit);
}

int context_fatigue_turns_remaining(void) {
    if (g_fatigue.ema_velocity <= 0.0)
        return -1;

    int remaining = g_fatigue.context_limit - g_fatigue.last_total;
    if (remaining <= 0)
        return 0;

    return (int)(remaining / g_fatigue.ema_velocity);
}

typedef enum {
    FATIGUE_GREEN = 0,
    FATIGUE_YELLOW = 1,
    FATIGUE_ORANGE = 2,
    FATIGUE_RED = 3,
} fatigue_level_t;

int context_fatigue_level(void) {
    int pct = context_fatigue_pct();
    if (pct < CTX_ZONE_WARNING_PCT)
        return FATIGUE_GREEN;
    if (pct < CTX_ZONE_COMPACT_PCT)
        return FATIGUE_YELLOW;
    if (pct < CTX_ZONE_BLOCKING_PCT)
        return FATIGUE_ORANGE;
    return FATIGUE_RED;
}

bool context_fatigue_needs_compact(void) {
    int pct = context_fatigue_pct();

    /* Hard threshold: already past compact zone */
    if (pct >= CTX_ZONE_COMPACT_PCT)
        return true;

    /* Predictive: velocity shows we'll hit the wall within 3 turns */
    int turns_left = context_fatigue_turns_remaining();
    if (turns_left >= 0 && turns_left <= 3 && pct >= CTX_ZONE_WARNING_PCT)
        return true;

    return false;
}

double context_fatigue_velocity(void) {
    return g_fatigue.ema_velocity;
}

/* ── Formatting ── */

char *context_fatigue_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    static const char *level_names[] = {"green", "yellow", "orange", "red"};
    int level = context_fatigue_level();
    int pct = context_fatigue_pct();
    int turns = context_fatigue_turns_remaining();

    strbuf_appendf(&sb, "Context: %d/%d tokens (%d%%) [%s] | velocity: %.0f tok/turn",
                   g_fatigue.last_total, g_fatigue.context_limit, pct, level_names[level],
                   g_fatigue.ema_velocity);

    if (turns >= 0)
        strbuf_appendf(&sb, " | ~%d turns remaining", turns);

    if (context_fatigue_needs_compact())
        strbuf_append_str(&sb, " | COMPACT RECOMMENDED");

    return strbuf_detach(&sb);
}

void context_fatigue_snapshot(context_fatigue_snapshot_t *out) {
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    long now_s = nc_wall_sec();
    out->count = g_fatigue.count;
    out->context_limit = g_fatigue.context_limit;
    out->ema_velocity = g_fatigue.ema_velocity;
    out->last_total = g_fatigue.last_total;
    out->initialized = g_fatigue.initialized;

    if (out->count < 0)
        out->count = 0;
    if (out->count > FATIGUE_SAMPLE_MAX)
        out->count = FATIGUE_SAMPLE_MAX;

    for (int i = 0; i < out->count; i++) {
        out->samples[i].turn = g_fatigue.samples[i].turn;
        out->samples[i].tokens_used = g_fatigue.samples[i].tokens_used;
        out->samples[i].tokens_added = g_fatigue.samples[i].tokens_added;
        out->samples[i].age_s = now_s - g_fatigue.samples[i].timestamp;
        if (out->samples[i].age_s < 0)
            out->samples[i].age_s = 0;
    }
}

void context_fatigue_restore(const context_fatigue_snapshot_t *snap) {
    context_fatigue_free();
    if (!snap) {
        context_fatigue_init(0);
        return;
    }

    context_fatigue_init(snap->context_limit);
    g_fatigue.ema_velocity = snap->ema_velocity;
    g_fatigue.last_total = snap->last_total;
    g_fatigue.initialized = snap->initialized;
    g_fatigue.count = snap->count;
    if (g_fatigue.count < 0)
        g_fatigue.count = 0;
    if (g_fatigue.count > FATIGUE_SAMPLE_MAX)
        g_fatigue.count = FATIGUE_SAMPLE_MAX;

    long now_s = nc_wall_sec();
    for (int i = 0; i < g_fatigue.count; i++) {
        g_fatigue.samples[i].turn = snap->samples[i].turn;
        g_fatigue.samples[i].tokens_used = snap->samples[i].tokens_used;
        g_fatigue.samples[i].tokens_added = snap->samples[i].tokens_added;
        g_fatigue.samples[i].timestamp = now_s - snap->samples[i].age_s;
        if (g_fatigue.samples[i].timestamp > now_s)
            g_fatigue.samples[i].timestamp = now_s;
    }
}
