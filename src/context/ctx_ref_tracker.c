/*
 * ctx_ref_tracker.c — Context reference tracking for smarter compaction.
 *
 * Tracks which file paths and resources are referenced during a session.
 * Helps the compaction pipeline preserve context about actively-used files
 * and avoid discarding references that the model still needs.
 */

#include "nevercode.h"

#define CTX_REF_MAX NC_CTX_REF_MAX

typedef struct {
    char path[NC_PATH_MAX];
    int mention_count;
    int last_turn;
    time_t last_time;
    bool pinned;
} ctx_ref_entry_t;

static struct {
    ctx_ref_entry_t entries[CTX_REF_MAX];
    int count;
    int current_turn;
} g_refs;

void ctx_ref_init(void) {
    memset(&g_refs, 0, sizeof(g_refs));
}

void ctx_ref_set_turn(int turn) {
    g_refs.current_turn = turn;
}

static ctx_ref_entry_t *find_entry(const char *path) {
    if (!path)
        return NULL;
    for (int i = 0; i < g_refs.count; i++) {
        if (strcmp(g_refs.entries[i].path, path) == 0)
            return &g_refs.entries[i];
    }
    return NULL;
}

void ctx_ref_track(const char *path) {
    if (!path || !path[0])
        return;

    ctx_ref_entry_t *e = find_entry(path);
    if (e) {
        e->mention_count++;
        e->last_turn = g_refs.current_turn;
        e->last_time = nc_time_now();
        return;
    }

    if (g_refs.count >= CTX_REF_MAX) {
        int oldest_idx = 0;
        int oldest_turn = g_refs.entries[0].last_turn;
        for (int i = 1; i < g_refs.count; i++) {
            if (!g_refs.entries[i].pinned && g_refs.entries[i].last_turn < oldest_turn) {
                oldest_turn = g_refs.entries[i].last_turn;
                oldest_idx = i;
            }
        }
        if (g_refs.entries[oldest_idx].pinned)
            return;
        g_refs.entries[oldest_idx] = (ctx_ref_entry_t){0};
        nc_strlcpy(g_refs.entries[oldest_idx].path, path, sizeof(g_refs.entries[oldest_idx].path));
        g_refs.entries[oldest_idx].mention_count = 1;
        g_refs.entries[oldest_idx].last_turn = g_refs.current_turn;
        g_refs.entries[oldest_idx].last_time = nc_time_now();
        return;
    }

    ctx_ref_entry_t *n = &g_refs.entries[g_refs.count++];
    memset(n, 0, sizeof(*n));
    nc_strlcpy(n->path, path, sizeof(n->path));
    n->mention_count = 1;
    n->last_turn = g_refs.current_turn;
    n->last_time = nc_time_now();
}

void ctx_ref_pin(const char *path) {
    ctx_ref_entry_t *e = find_entry(path);
    if (e)
        e->pinned = true;
}

bool ctx_ref_is_active(const char *path, int recency_turns) {
    ctx_ref_entry_t *e = find_entry(path);
    if (!e)
        return false;
    if (e->pinned)
        return true;
    return (g_refs.current_turn - e->last_turn) <= recency_turns;
}

int ctx_ref_count(void) {
    return g_refs.count;
}

int ctx_ref_active_count(int recency_turns) {
    int n = 0;
    for (int i = 0; i < g_refs.count; i++) {
        if (g_refs.entries[i].pinned ||
            (g_refs.current_turn - g_refs.entries[i].last_turn) <= recency_turns)
            n++;
    }
    return n;
}

char *ctx_ref_format_active(int recency_turns) {
    strbuf_t sb;
    strbuf_init(&sb);

    int active = 0;
    for (int i = 0; i < g_refs.count; i++) {
        ctx_ref_entry_t *e = &g_refs.entries[i];
        if (!e->pinned && (g_refs.current_turn - e->last_turn) > recency_turns)
            continue;
        if (active == 0)
            strbuf_append_str(&sb, "Active file references:\n");
        strbuf_appendf(&sb, "  %s (mentions: %d%s)\n", e->path, e->mention_count,
                       e->pinned ? ", pinned" : "");
        active++;
    }

    if (active == 0) {
        strbuf_free(&sb);
        return NULL;
    }
    return strbuf_detach(&sb);
}

void ctx_ref_clear(void) {
    memset(&g_refs, 0, sizeof(g_refs));
}
