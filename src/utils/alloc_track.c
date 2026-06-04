/*
 * alloc_track.c — Debug-mode allocation tracker
 *
 * When compiled with -DNEVERCODE_DEBUG_ALLOC, wraps malloc/realloc/free
 * with instrumentation that tracks:
 *   - active allocations (pointer → size + call site)
 *   - total bytes allocated and freed
 *   - high-water mark
 *   - leak report on shutdown
 *
 * Disabled in release builds — zero overhead.
 */

#include "nevercode.h"

#ifdef NEVERCODE_DEBUG_ALLOC

typedef struct alloc_entry {
    void *ptr;
    size_t size;
    const char *file;
    int line;
    struct alloc_entry *next;
} alloc_entry_t;

static struct {
    alloc_entry_t *buckets[NC_ALLOC_TRACK_BUCKETS];
    int count;
    size_t total_alloc;
    size_t total_freed;
    size_t active_bytes;
    size_t high_water;
    int alloc_calls;
    int free_calls;
    pthread_mutex_t lock;
    bool initialized;
} g_alloc;

static unsigned bucket_idx(const void *ptr) {
    uintptr_t h = (uintptr_t)ptr;
    h = (h >> 4) ^ (h >> 16);
    return (unsigned)(h % NC_ALLOC_TRACK_BUCKETS);
}

void alloc_track_init(void) {
    memset(&g_alloc, 0, sizeof(g_alloc));
    pthread_mutex_init(&g_alloc.lock, NULL);
    g_alloc.initialized = true;
}

void alloc_track_free(void) {
    if (!g_alloc.initialized)
        return;
    for (int i = 0; i < NC_ALLOC_TRACK_BUCKETS; i++) {
        alloc_entry_t *e = g_alloc.buckets[i];
        while (e) {
            alloc_entry_t *next = e->next;
            free(e);
            e = next;
        }
        g_alloc.buckets[i] = NULL;
    }
    pthread_mutex_destroy(&g_alloc.lock);
    g_alloc.initialized = false;
}

void alloc_track_record(void *ptr, size_t size, const char *file, int line) {
    if (!g_alloc.initialized || !ptr)
        return;

    alloc_entry_t *e = malloc(sizeof(*e));
    if (!e)
        return;

    e->ptr = ptr;
    e->size = size;
    e->file = file;
    e->line = line;

    unsigned idx = bucket_idx(ptr);

    pthread_mutex_lock(&g_alloc.lock);
    e->next = g_alloc.buckets[idx];
    g_alloc.buckets[idx] = e;
    g_alloc.count++;
    g_alloc.total_alloc += size;
    g_alloc.active_bytes += size;
    g_alloc.alloc_calls++;
    if (g_alloc.active_bytes > g_alloc.high_water)
        g_alloc.high_water = g_alloc.active_bytes;
    pthread_mutex_unlock(&g_alloc.lock);
}

void alloc_track_release(void *ptr) {
    if (!g_alloc.initialized || !ptr)
        return;

    unsigned idx = bucket_idx(ptr);

    pthread_mutex_lock(&g_alloc.lock);
    alloc_entry_t **pp = &g_alloc.buckets[idx];
    while (*pp) {
        if ((*pp)->ptr == ptr) {
            alloc_entry_t *found = *pp;
            *pp = found->next;
            g_alloc.total_freed += found->size;
            g_alloc.active_bytes -= found->size;
            g_alloc.count--;
            g_alloc.free_calls++;
            pthread_mutex_unlock(&g_alloc.lock);
            free(found);
            return;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_alloc.lock);
}

void alloc_track_update(void *old_ptr, void *new_ptr, size_t new_size, const char *file, int line) {
    if (!g_alloc.initialized)
        return;
    if (old_ptr)
        alloc_track_release(old_ptr);
    if (new_ptr)
        alloc_track_record(new_ptr, new_size, file, line);
}

int alloc_track_leak_count(void) {
    if (!g_alloc.initialized)
        return 0;
    pthread_mutex_lock(&g_alloc.lock);
    int c = g_alloc.count;
    pthread_mutex_unlock(&g_alloc.lock);
    return c;
}

size_t alloc_track_active_bytes(void) {
    if (!g_alloc.initialized)
        return 0;
    pthread_mutex_lock(&g_alloc.lock);
    size_t b = g_alloc.active_bytes;
    pthread_mutex_unlock(&g_alloc.lock);
    return b;
}

size_t alloc_track_high_water(void) {
    return g_alloc.initialized ? g_alloc.high_water : 0;
}

char *alloc_track_report(int max_entries) {
    if (!g_alloc.initialized)
        return nc_xstrdup("(alloc tracker not initialized)\n");

    strbuf_t sb;
    strbuf_init(&sb);

    pthread_mutex_lock(&g_alloc.lock);

    char line[NC_MSG_MAX];
    snprintf(line, sizeof(line),
             "Allocation Report:\n"
             "  active     %d allocations, %zu bytes\n"
             "  total      alloc=%zu  freed=%zu\n"
             "  high-water %zu bytes\n"
             "  calls      malloc=%d  free=%d\n",
             g_alloc.count, g_alloc.active_bytes, g_alloc.total_alloc, g_alloc.total_freed,
             g_alloc.high_water, g_alloc.alloc_calls, g_alloc.free_calls);
    strbuf_append_str(&sb, line);

    if (g_alloc.count > 0 && max_entries > 0) {
        strbuf_append_str(&sb, "\n  Leaked allocations:\n");
        int shown = 0;
        for (int i = 0; i < NC_ALLOC_TRACK_BUCKETS && shown < max_entries; i++) {
            alloc_entry_t *e = g_alloc.buckets[i];
            while (e && shown < max_entries) {
                snprintf(line, sizeof(line), "    %p  %6zu bytes  %s:%d\n", e->ptr, e->size,
                         e->file ? e->file : "?", e->line);
                strbuf_append_str(&sb, line);
                shown++;
                e = e->next;
            }
        }
        if (g_alloc.count > max_entries) {
            snprintf(line, sizeof(line), "    ... and %d more\n", g_alloc.count - max_entries);
            strbuf_append_str(&sb, line);
        }
    }

    pthread_mutex_unlock(&g_alloc.lock);
    return strbuf_detach(&sb);
}

#else /* !NEVERCODE_DEBUG_ALLOC */

void alloc_track_init(void) {}
void alloc_track_free(void) {}
void alloc_track_record(void *ptr, size_t size, const char *file, int line) {
    (void)ptr;
    (void)size;
    (void)file;
    (void)line;
}
void alloc_track_release(void *ptr) {
    (void)ptr;
}
void alloc_track_update(void *old_ptr, void *new_ptr, size_t new_size, const char *file, int line) {
    (void)old_ptr;
    (void)new_ptr;
    (void)new_size;
    (void)file;
    (void)line;
}
int alloc_track_leak_count(void) {
    return 0;
}
size_t alloc_track_active_bytes(void) {
    return 0;
}
size_t alloc_track_high_water(void) {
    return 0;
}
char *alloc_track_report(int max_entries) {
    (void)max_entries;
    return nc_xstrdup("(debug alloc tracking disabled)\n");
}

#endif /* NEVERCODE_DEBUG_ALLOC */
