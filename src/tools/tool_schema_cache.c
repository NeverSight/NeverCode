/*
 * tool_schema_cache.c — Tool schema caching for performance
 *
 * Caches tool JSON schemas to avoid regenerating them on every
 * API call. Schemas are keyed by tool name and invalidated when
 * tools are added/removed or feature flags change.
 */

#include "nevercode.h"

/* TSC_MAX_ENTRIES, TSC_DEFAULT_TTL_S, TSC_NAME_MAX in defaults.h */

typedef struct {
    char name[TSC_NAME_MAX];
    char *schema_json;
    time_t cached_at;
    int ttl;
    bool valid;
} tsc_entry_t;

typedef struct {
    tsc_entry_t entries[TSC_MAX_ENTRIES];
    int count;
    int hits;
    int misses;
    int evictions;
    int generation;
} tsc_cache_t;

static tsc_cache_t g_tsc;

void tsc_init(void) {
    memset(&g_tsc, 0, sizeof(g_tsc));
}

void tsc_clear(void) {
    for (int i = 0; i < g_tsc.count; i++)
        free(g_tsc.entries[i].schema_json);
    int gen = g_tsc.generation + 1;
    memset(&g_tsc, 0, sizeof(g_tsc));
    g_tsc.generation = gen;
}

static int find_entry(const char *name) {
    for (int i = 0; i < g_tsc.count; i++)
        if (g_tsc.entries[i].valid && strcmp(g_tsc.entries[i].name, name) == 0)
            return i;
    return -1;
}

static bool is_expired(const tsc_entry_t *e) {
    if (e->ttl <= 0)
        return false;
    return difftime(nc_time_now(), e->cached_at) > e->ttl;
}

const char *tsc_get(const char *name) {
    int idx = find_entry(name);
    if (idx < 0) {
        g_tsc.misses++;
        return NULL;
    }
    tsc_entry_t *e = &g_tsc.entries[idx];
    if (is_expired(e)) {
        e->valid = false;
        free(e->schema_json);
        e->schema_json = NULL;
        g_tsc.evictions++;
        g_tsc.misses++;
        return NULL;
    }
    g_tsc.hits++;
    return e->schema_json;
}

int tsc_put(const char *name, const char *schema_json) {
    if (!name || !schema_json)
        return -1;

    int idx = find_entry(name);
    if (idx >= 0) {
        tsc_entry_t *e = &g_tsc.entries[idx];
        free(e->schema_json);
        e->schema_json = nc_xstrdup(schema_json);
        e->cached_at = nc_time_now();
        e->valid = true;
        return 0;
    }

    if (g_tsc.count >= TSC_MAX_ENTRIES) {
        /* evict oldest */
        int oldest = 0;
        for (int i = 1; i < g_tsc.count; i++)
            if (g_tsc.entries[i].cached_at < g_tsc.entries[oldest].cached_at)
                oldest = i;
        free(g_tsc.entries[oldest].schema_json);
        g_tsc.entries[oldest].valid = false;
        idx = oldest;
        g_tsc.evictions++;
    } else {
        idx = g_tsc.count++;
    }

    tsc_entry_t *e = &g_tsc.entries[idx];
    nc_strlcpy(e->name, name, sizeof(e->name));
    e->schema_json = nc_xstrdup(schema_json);
    e->cached_at = nc_time_now();
    e->ttl = TSC_DEFAULT_TTL_S;
    e->valid = true;
    return 0;
}

int tsc_invalidate(const char *name) {
    int idx = find_entry(name);
    if (idx < 0)
        return -1;
    g_tsc.entries[idx].valid = false;
    free(g_tsc.entries[idx].schema_json);
    g_tsc.entries[idx].schema_json = NULL;
    return 0;
}

void tsc_invalidate_all(void) {
    tsc_clear();
}

int tsc_count(void) {
    int n = 0;
    for (int i = 0; i < g_tsc.count; i++)
        if (g_tsc.entries[i].valid)
            n++;
    return n;
}

char *tsc_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Schema cache: %d entries, %d hits, %d misses, %d evictions (gen %d)\n",
                   tsc_count(), g_tsc.hits, g_tsc.misses, g_tsc.evictions, g_tsc.generation);
    return strbuf_detach(&sb);
}
