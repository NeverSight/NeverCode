/*
 * prompt_section.c — Modular system prompt section registry with memoization.
 *
 * Each section is registered with a compute function. The result is cached
 * until explicitly invalidated (/clear, /compact, session reset). Volatile
 * sections are recomputed every turn — use sparingly as they break the
 * prompt cache when the value changes.
 */

#include "nevercode.h"

typedef struct {
    char name[PSEC_NAME_MAX];
    char *cached_value;
    psec_compute_fn compute;
    void *user_data;
    bool is_volatile;
    bool has_cached;
} psec_entry_t;

static struct {
    psec_entry_t entries[PSEC_MAX_SECTIONS];
    int count;
} g_psec;

void psec_init(void) {
    for (int i = 0; i < g_psec.count; i++)
        free(g_psec.entries[i].cached_value);
    memset(&g_psec, 0, sizeof(g_psec));
}

int psec_register(const char *name, psec_compute_fn fn, void *user_data, bool is_volatile) {
    if (!name || !fn || g_psec.count >= PSEC_MAX_SECTIONS)
        return -1;
    for (int i = 0; i < g_psec.count; i++) {
        if (strcmp(g_psec.entries[i].name, name) == 0)
            return -1;
    }
    psec_entry_t *e = &g_psec.entries[g_psec.count++];
    nc_strlcpy(e->name, name, PSEC_NAME_MAX);
    e->compute = fn;
    e->user_data = user_data;
    e->is_volatile = is_volatile;
    e->cached_value = NULL;
    e->has_cached = false;
    return 0;
}

const char *psec_resolve(const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; i < g_psec.count; i++) {
        psec_entry_t *e = &g_psec.entries[i];
        if (strcmp(e->name, name) != 0)
            continue;
        if (!e->is_volatile && e->has_cached)
            return e->cached_value;
        free(e->cached_value);
        e->cached_value = e->compute(e->user_data);
        e->has_cached = true;
        return e->cached_value;
    }
    return NULL;
}

int psec_resolve_all(char **out_buf, int max_parts) {
    if (!out_buf || max_parts <= 0)
        return 0;
    int n = 0;
    for (int i = 0; i < g_psec.count && n < max_parts; i++) {
        const char *val = psec_resolve(g_psec.entries[i].name);
        if (val && *val)
            out_buf[n++] = (char *)val;
    }
    return n;
}

void psec_invalidate(const char *name) {
    if (!name)
        return;
    for (int i = 0; i < g_psec.count; i++) {
        if (strcmp(g_psec.entries[i].name, name) == 0) {
            free(g_psec.entries[i].cached_value);
            g_psec.entries[i].cached_value = NULL;
            g_psec.entries[i].has_cached = false;
            return;
        }
    }
}

void psec_invalidate_all(void) {
    for (int i = 0; i < g_psec.count; i++) {
        free(g_psec.entries[i].cached_value);
        g_psec.entries[i].cached_value = NULL;
        g_psec.entries[i].has_cached = false;
    }
}

int psec_count(void) {
    return g_psec.count;
}

bool psec_is_volatile(const char *name) {
    if (!name)
        return false;
    for (int i = 0; i < g_psec.count; i++) {
        if (strcmp(g_psec.entries[i].name, name) == 0)
            return g_psec.entries[i].is_volatile;
    }
    return false;
}
