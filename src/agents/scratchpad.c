/*
 * scratchpad.c — Shared key-value workspace for coordinator-worker collaboration
 *
 * Provides a temporary key-value scratch area backed by an in-memory
 * store with optional file persistence.  Workers write partial results;
 * the coordinator reads them to synthesize a final answer.
 */

#include "nevercode.h"

typedef struct {
    char *key;
    char *value;
    char *author;
    long updated_at;
} scratch_entry_t;

static scratch_entry_t g_entries[SCRATCHPAD_MAX_ENTRIES];
static int g_count = 0;
static bool g_initialized = false;

void scratchpad_init(void) {
    scratchpad_free();
    g_initialized = true;
}

void scratchpad_free(void) {
    for (int i = 0; i < g_count; i++) {
        free(g_entries[i].key);
        free(g_entries[i].value);
        free(g_entries[i].author);
    }
    g_count = 0;
    g_initialized = false;
}

static int find_key(const char *key) {
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].key, key) == 0)
            return i;
    }
    return -1;
}

int scratchpad_write(const char *key, const char *value, const char *author) {
    if (!key || !value)
        return -1;
    if (!g_initialized)
        scratchpad_init();

    int idx = find_key(key);
    if (idx >= 0) {
        free(g_entries[idx].value);
        free(g_entries[idx].author);
        g_entries[idx].value = nc_xstrdup(value);
        g_entries[idx].author = author ? nc_xstrdup(author) : NULL;
        g_entries[idx].updated_at = nc_wall_sec();
        return 0;
    }

    if (g_count >= SCRATCHPAD_MAX_ENTRIES)
        return -1;

    scratch_entry_t *e = &g_entries[g_count++];
    e->key = nc_xstrdup(key);
    e->value = nc_xstrdup(value);
    e->author = author ? nc_xstrdup(author) : NULL;
    e->updated_at = nc_wall_sec();
    return 0;
}

const char *scratchpad_read(const char *key) {
    int idx = find_key(key);
    return idx >= 0 ? g_entries[idx].value : NULL;
}

int scratchpad_remove(const char *key) {
    int idx = find_key(key);
    if (idx < 0)
        return -1;
    free(g_entries[idx].key);
    free(g_entries[idx].value);
    free(g_entries[idx].author);
    if (idx < g_count - 1)
        memmove(&g_entries[idx], &g_entries[idx + 1],
                (g_count - 1 - idx) * sizeof(scratch_entry_t));
    g_count--;
    return 0;
}

int scratchpad_count(void) {
    return g_count;
}

int scratchpad_keys(const char **out, int max) {
    int n = NC_MIN(g_count, max);
    for (int i = 0; i < n; i++)
        out[i] = g_entries[i].key;
    return n;
}

char *scratchpad_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "# Scratchpad\n\n");

    if (g_count == 0) {
        strbuf_append_str(&sb, "(empty)\n");
        return strbuf_detach(&sb);
    }

    for (int i = 0; i < g_count; i++) {
        strbuf_appendf(&sb, "## %s", g_entries[i].key);
        if (g_entries[i].author)
            strbuf_appendf(&sb, " [%s]", g_entries[i].author);
        strbuf_append_str(&sb, "\n");
        strbuf_append_str(&sb, g_entries[i].value);
        strbuf_append_str(&sb, "\n\n");
    }
    return strbuf_detach(&sb);
}

int scratchpad_persist(const char *path) {
    if (!path)
        return -1;
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    for (int i = 0; i < g_count; i++) {
        fprintf(f, "--- %s ---\n", g_entries[i].key);
        if (g_entries[i].author)
            fprintf(f, "author: %s\n", g_entries[i].author);
        fprintf(f, "%s\n", g_entries[i].value);
    }
    int rc = fflush(f) ? -1 : 0;
    fclose(f);
    return rc;
}
