/*
 * stream_ctrl.c — Streaming response controller with flow control.
 *
 * Manages chunk-based streaming from API responses with buffering,
 * backpressure detection, commit ticks for UI rendering, and
 * cancellation support.
 */

#include "nevercode.h"

/* SC_MAX_CHUNKS, SC_COMMIT_TICK_MS, SC_MAX_PENDING in defaults.h */
#ifndef SC_CHUNK_BUF
#define SC_CHUNK_BUF NC_CMD_MAX
#endif

typedef enum {
    SC_IDLE = 0,
    SC_STREAMING,
    SC_PAUSED,
    SC_CANCELLED,
    SC_COMPLETE,
    SC_ERROR
} sc_phase_t;

typedef struct {
    char *data;
    int len;
    long ts_ms;
    bool is_final;
} sc_chunk_t;

static struct {
    sc_phase_t phase;
    sc_chunk_t *chunks;
    int chunk_count;
    int chunk_cap;

    strbuf_t assembled;
    int committed_idx;
    long first_chunk_ms;
    long last_commit_ms;
    long start_ms;

    int total_bytes;
    int total_chunks;
    int commit_ticks;
    bool backpressure;
} g_sc;

void stream_ctrl_init(void) {
    memset(&g_sc, 0, sizeof(g_sc));
    g_sc.phase = SC_IDLE;
    g_sc.chunk_cap = SC_INITIAL_CHUNK_CAP;
    g_sc.chunks = calloc((size_t)g_sc.chunk_cap, sizeof(sc_chunk_t));
    if (!g_sc.chunks)
        g_sc.chunk_cap = 0;
    strbuf_init(&g_sc.assembled);
}

void stream_ctrl_free(void) {
    for (int i = 0; i < g_sc.chunk_count; i++)
        free(g_sc.chunks[i].data);
    free(g_sc.chunks);
    strbuf_free(&g_sc.assembled);
    memset(&g_sc, 0, sizeof(g_sc));
}

void stream_ctrl_reset(void) {
    for (int i = 0; i < g_sc.chunk_count; i++)
        free(g_sc.chunks[i].data);
    g_sc.chunk_count = 0;
    g_sc.committed_idx = 0;
    strbuf_clear(&g_sc.assembled);
    g_sc.phase = SC_IDLE;
    g_sc.first_chunk_ms = 0;
    g_sc.last_commit_ms = 0;
    g_sc.start_ms = 0;
    g_sc.backpressure = false;
}

int stream_ctrl_begin(void) {
    if (g_sc.phase != SC_IDLE && g_sc.phase != SC_COMPLETE)
        stream_ctrl_reset();
    g_sc.phase = SC_STREAMING;
    g_sc.start_ms = nc_mono_ms();
    return 0;
}

static void _ensure_cap(void) {
    if (g_sc.chunk_count < g_sc.chunk_cap)
        return;
    int new_cap = g_sc.chunk_cap < 1 ? SC_INITIAL_CHUNK_CAP : g_sc.chunk_cap * 2;
    if (new_cap > SC_MAX_CHUNKS)
        new_cap = SC_MAX_CHUNKS;
    sc_chunk_t *tmp = realloc(g_sc.chunks, (size_t)new_cap * sizeof(sc_chunk_t));
    if (!tmp)
        return;
    g_sc.chunks = tmp;
    memset(&g_sc.chunks[g_sc.chunk_cap], 0,
           (size_t)(new_cap - g_sc.chunk_cap) * sizeof(sc_chunk_t));
    g_sc.chunk_cap = new_cap;
}

int stream_ctrl_feed(const char *data, int len, bool is_final) {
    if (g_sc.phase != SC_STREAMING)
        return -1;
    if (g_sc.chunk_count >= SC_MAX_CHUNKS) {
        g_sc.backpressure = true;
        return -1;
    }

    _ensure_cap();
    sc_chunk_t *c = &g_sc.chunks[g_sc.chunk_count++];
    c->data = malloc((size_t)len + 1);
    if (!c->data) {
        g_sc.chunk_count--;
        return -1;
    }
    memcpy(c->data, data, (size_t)len);
    c->data[len] = '\0';
    c->len = len;
    c->ts_ms = nc_mono_ms();
    c->is_final = is_final;

    if (g_sc.first_chunk_ms == 0)
        g_sc.first_chunk_ms = c->ts_ms;

    g_sc.total_bytes += len;
    g_sc.total_chunks++;

    strbuf_append(&g_sc.assembled, data, (size_t)len);

    if (is_final)
        g_sc.phase = SC_COMPLETE;

    return 0;
}

int stream_ctrl_commit(void) {
    if (g_sc.committed_idx >= g_sc.chunk_count)
        return 0;

    int committed = g_sc.chunk_count - g_sc.committed_idx;
    g_sc.committed_idx = g_sc.chunk_count;
    g_sc.last_commit_ms = nc_mono_ms();
    g_sc.commit_ticks++;
    return committed;
}

bool stream_ctrl_should_commit(void) {
    if (g_sc.committed_idx >= g_sc.chunk_count)
        return false;
    if (g_sc.phase == SC_COMPLETE)
        return true;
    long now = nc_mono_ms();
    return (now - g_sc.last_commit_ms) >= SC_COMMIT_TICK_MS;
}

const char *stream_ctrl_assembled(void) {
    return g_sc.assembled.data;
}

int stream_ctrl_assembled_len(void) {
    return (int)g_sc.assembled.len;
}

void stream_ctrl_pause(void) {
    if (g_sc.phase == SC_STREAMING)
        g_sc.phase = SC_PAUSED;
}

void stream_ctrl_resume(void) {
    if (g_sc.phase == SC_PAUSED)
        g_sc.phase = SC_STREAMING;
}

void stream_ctrl_cancel(void) {
    g_sc.phase = SC_CANCELLED;
}

sc_phase_t stream_ctrl_phase(void) {
    return g_sc.phase;
}

bool stream_ctrl_complete(void) {
    return g_sc.phase == SC_COMPLETE;
}

bool stream_ctrl_active(void) {
    return g_sc.phase == SC_STREAMING || g_sc.phase == SC_PAUSED;
}

long stream_ctrl_ttfb_ms(void) {
    if (g_sc.first_chunk_ms == 0 || g_sc.start_ms == 0)
        return 0;
    return g_sc.first_chunk_ms - g_sc.start_ms;
}

long stream_ctrl_elapsed_ms(void) {
    if (g_sc.start_ms == 0)
        return 0;
    return nc_mono_ms() - g_sc.start_ms;
}

int stream_ctrl_chunk_count(void) {
    return g_sc.chunk_count;
}

int stream_ctrl_pending_count(void) {
    return g_sc.chunk_count - g_sc.committed_idx;
}

int stream_ctrl_total_bytes(void) {
    return g_sc.total_bytes;
}

static const char *_phase_name(sc_phase_t p) {
    switch (p) {
    case SC_IDLE:
        return "idle";
    case SC_STREAMING:
        return "streaming";
    case SC_PAUSED:
        return "paused";
    case SC_CANCELLED:
        return "cancelled";
    case SC_COMPLETE:
        return "complete";
    case SC_ERROR:
        return "error";
    }
    return "unknown";
}

char *stream_ctrl_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Stream: phase=%s chunks=%d bytes=%d ttfb=%ldms\n", _phase_name(g_sc.phase),
                   g_sc.chunk_count, g_sc.total_bytes, stream_ctrl_ttfb_ms());
    strbuf_appendf(&sb, "  commits=%d pending=%d bp=%s elapsed=%ldms\n", g_sc.commit_ticks,
                   stream_ctrl_pending_count(), g_sc.backpressure ? "yes" : "no",
                   stream_ctrl_elapsed_ms());
    return strbuf_detach(&sb);
}
