/*
 * file_read_cache.c — mtime-based file content cache.
 *
 * Eliminates redundant disk reads when the same file is accessed
 * multiple times within a turn (e.g. Read-then-Edit).  Cache entries
 * are automatically invalidated when the file's modification time
 * changes.
 */

#include "nevercode.h"

typedef struct {
    char path[NC_PATH_MAX];
    char *content;
    size_t len;
    time_t mtime;
} frc_entry_t;

static frc_entry_t g_entries[FRC_MAX_ENTRIES];
static int g_count = 0;

static int find_entry(const char *path) {
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].path, path) == 0)
            return i;
    }
    return -1;
}

static void evict_oldest(void) {
    if (g_count <= 0)
        return;
    free(g_entries[0].content);
    memmove(&g_entries[0], &g_entries[1], sizeof(frc_entry_t) * (g_count - 1));
    g_count--;
}

const char *frc_get(const char *path, size_t *out_len) {
    if (!path)
        return NULL;

    int idx = find_entry(path);
    if (idx < 0)
        return NULL;

    struct stat st;
    if (stat(path, &st) != 0) {
        /* File deleted — remove from cache */
        free(g_entries[idx].content);
        memmove(&g_entries[idx], &g_entries[idx + 1], sizeof(frc_entry_t) * (g_count - idx - 1));
        g_count--;
        return NULL;
    }

    if (st.st_mtime != g_entries[idx].mtime) {
        /* Stale — invalidate */
        free(g_entries[idx].content);
        memmove(&g_entries[idx], &g_entries[idx + 1], sizeof(frc_entry_t) * (g_count - idx - 1));
        g_count--;
        return NULL;
    }

    if (out_len)
        *out_len = g_entries[idx].len;
    return g_entries[idx].content;
}

int frc_put(const char *path, const char *content, size_t len) {
    if (!path || !content)
        return -1;

    struct stat st;
    if (stat(path, &st) != 0)
        return -1;

    int idx = find_entry(path);
    if (idx >= 0) {
        free(g_entries[idx].content);
        g_entries[idx].content = malloc(len + 1);
        if (!g_entries[idx].content)
            return -1;
        memcpy(g_entries[idx].content, content, len);
        g_entries[idx].content[len] = '\0';
        g_entries[idx].len = len;
        g_entries[idx].mtime = st.st_mtime;
        return 0;
    }

    if (g_count >= FRC_MAX_ENTRIES)
        evict_oldest();

    frc_entry_t *e = &g_entries[g_count];
    nc_strlcpy(e->path, path, sizeof(e->path));
    e->content = malloc(len + 1);
    if (!e->content)
        return -1;
    memcpy(e->content, content, len);
    e->content[len] = '\0';
    e->len = len;
    e->mtime = st.st_mtime;
    g_count++;
    return 0;
}

void frc_invalidate(const char *path) {
    if (!path)
        return;
    int idx = find_entry(path);
    if (idx < 0)
        return;
    free(g_entries[idx].content);
    memmove(&g_entries[idx], &g_entries[idx + 1], sizeof(frc_entry_t) * (g_count - idx - 1));
    g_count--;
}

void frc_clear(void) {
    for (int i = 0; i < g_count; i++)
        free(g_entries[i].content);
    g_count = 0;
}

int frc_size(void) {
    return g_count;
}
