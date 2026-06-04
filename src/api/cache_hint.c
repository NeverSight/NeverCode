/*
 * cache_hint.c — Prompt prefix cache tracking and breakpoint detection.
 *
 * Tracks DJB2 fingerprints of system prompts AND tool schemas across turns.
 * Detects when the combined prefix changes (cache break) and reports
 * which component changed: system prompt, tool schema, or model/mode.
 */

#include "nevercode.h"

typedef enum {
    CACHE_BREAK_NONE = 0,
    CACHE_BREAK_SYSTEM,
    CACHE_BREAK_TOOLS,
    CACHE_BREAK_MODEL,
    CACHE_BREAK_BOTH,
} cache_break_source_t;

typedef struct {
    unsigned long last_fingerprint;
    unsigned long last_tools_hash;
    unsigned long last_model_hash;
    int last_len;
    int cache_hits;
    int cache_misses;
    int breaks_detected;
    int breaks_avoided;
    int tool_breaks;
    int model_breaks;
    int call_count;
    cache_break_source_t last_break_source;
    bool tools_hash_set;
    bool model_hash_set;
    bool enabled;
} cache_hint_ctx_t;

static cache_hint_ctx_t g_ch = {0};

static unsigned long djb2(const char *s, int len) {
    unsigned long h = NC_DJB2_SEED;
    for (int i = 0; i < len; i++)
        h = ((h << 5) + h) + (unsigned char)s[i];
    return h;
}

void cache_hint_init(void) {
    memset(&g_ch, 0, sizeof(g_ch));
    g_ch.enabled = true;
}

void cache_hint_set_enabled(bool e) {
    g_ch.enabled = e;
}
bool cache_hint_enabled(void) {
    return g_ch.enabled;
}

static int find_common_prefix(const char *a, int al, const char *b, int bl) {
    int m = NC_MIN(al, bl);
    int i = 0;
    while (i < m && a[i] == b[i])
        i++;
    return i;
}

int cache_hint_analyze(const char *sys, const char *prev) {
    if (!g_ch.enabled || !sys)
        return -1;

    int cur_len = (int)strlen(sys);

    if (!prev || !prev[0]) {
        g_ch.last_fingerprint = djb2(sys, cur_len);
        g_ch.last_len = cur_len;
        g_ch.cache_misses++;
        return -1;
    }

    int prev_len = (int)strlen(prev);
    unsigned long fp = djb2(sys, cur_len);

    if (fp == g_ch.last_fingerprint && cur_len == g_ch.last_len) {
        g_ch.cache_hits++;
        return -1;
    }

    int prefix = find_common_prefix(prev, prev_len, sys, cur_len);
    int min_len = NC_MIN(prev_len, cur_len);
    int threshold = min_len * 4 / 5;

    g_ch.last_fingerprint = fp;
    g_ch.last_len = cur_len;

    if (prefix >= threshold && prefix > 100) {
        g_ch.breaks_detected++;
        g_ch.breaks_avoided++;
        return prefix;
    }

    g_ch.breaks_detected++;
    g_ch.cache_misses++;
    return -1;
}

void cache_hint_record_hit(void) {
    g_ch.cache_hits++;
}
void cache_hint_record_miss(void) {
    g_ch.cache_misses++;
}
int cache_hint_hits(void) {
    return g_ch.cache_hits;
}
int cache_hint_misses(void) {
    return g_ch.cache_misses;
}

/*
 * Track tool schema fingerprint across turns. Compute a combined hash
 * of all tool names + descriptions so we detect when tool definitions
 * are added/removed/modified (which invalidates the prompt cache).
 */
void cache_hint_set_tools_hash(const tool_def_t *tools, int num_tools) {
    if (!g_ch.enabled)
        return;
    unsigned long h = NC_DJB2_SEED;
    for (int i = 0; i < num_tools; i++) {
        if (tools[i].name) {
            int n = (int)strlen(tools[i].name);
            for (int j = 0; j < n; j++)
                h = ((h << 5) + h) + (unsigned char)tools[i].name[j];
        }
        if (tools[i].description) {
            int n = (int)strlen(tools[i].description);
            for (int j = 0; j < n; j++)
                h = ((h << 5) + h) + (unsigned char)tools[i].description[j];
        }
    }
    if (g_ch.tools_hash_set && h != g_ch.last_tools_hash) {
        g_ch.tool_breaks++;
        g_ch.breaks_detected++;
        g_ch.last_break_source = CACHE_BREAK_TOOLS;
    }
    g_ch.last_tools_hash = h;
    g_ch.tools_hash_set = true;
}

unsigned long cache_hint_tools_hash(void) {
    return g_ch.last_tools_hash;
}

void cache_hint_set_model_hash(const char *model) {
    if (!g_ch.enabled || !model)
        return;
    unsigned long h = djb2(model, (int)strlen(model));
    if (g_ch.model_hash_set && h != g_ch.last_model_hash) {
        g_ch.model_breaks++;
        g_ch.breaks_detected++;
        g_ch.last_break_source = CACHE_BREAK_MODEL;
    }
    g_ch.last_model_hash = h;
    g_ch.model_hash_set = true;
}

void cache_hint_tick(void) {
    g_ch.call_count++;
}

int cache_hint_tool_breaks(void) {
    return g_ch.tool_breaks;
}
int cache_hint_model_breaks(void) {
    return g_ch.model_breaks;
}
int cache_hint_call_count(void) {
    return g_ch.call_count;
}

/*
 * Select up to 4 cache breakpoint positions for the native provider:
 *   1. System prompt (position 0)
 *   2-4. Last 3 non-system messages (rolling window)
 *
 * Returns the number of breakpoints written to out_positions[].
 */
int cache_hint_breakpoints(const message_t *msgs, int n, int out_positions[], int max_out) {
    if (!msgs || n <= 0 || !out_positions || max_out < 1)
        return 0;

    int bp = 0;

    out_positions[bp++] = 0;
    if (bp >= max_out)
        return bp;

    int last_3[3] = {-1, -1, -1};
    int found = 0;
    for (int i = n - 1; i > 0 && found < 3; i--) {
        if (msgs[i].role != ROLE_SYSTEM) {
            last_3[found++] = i;
        }
    }

    for (int i = found - 1; i >= 0 && bp < max_out; i--) {
        if (last_3[i] >= 0)
            out_positions[bp++] = last_3[i];
    }

    return bp;
}

char *cache_hint_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    int total = g_ch.cache_hits + g_ch.cache_misses;
    int rate = total > 0 ? (g_ch.cache_hits * 100 / total) : 0;

    strbuf_appendf(&sb,
                   "Cache: %d/%d hits (%d%%), breaks=%d (sys=%d, tool=%d, model=%d), avoided=%d, "
                   "calls=%d",
                   g_ch.cache_hits, total, rate, g_ch.breaks_detected,
                   g_ch.breaks_detected - g_ch.tool_breaks - g_ch.model_breaks, g_ch.tool_breaks,
                   g_ch.model_breaks, g_ch.breaks_avoided, g_ch.call_count);

    return strbuf_detach(&sb);
}
