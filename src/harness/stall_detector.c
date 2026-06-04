/*
 * stall_detector.c — Agent loop watchdog / heartbeat monitor
 *
 * Detects when the agent loop has stalled (no meaningful progress
 * for N consecutive turns) and recommends corrective actions.
 * Tracks tool call diversity, output size variance, and elapsed
 * wall-clock time to distinguish genuine stalls from slow-but-progressing work.
 */

#include "harness.h"

static stall_ctx_t g_stall;
static void reset_read_freq(void);

void stall_detector_init(void) {
    memset(&g_stall, 0, sizeof(g_stall));
    g_stall.last_progress_time = nc_wall_sec();
    reset_read_freq();
}

void stall_detector_free(void) {
    for (int i = 0; i < g_stall.window_count; i++)
        free(g_stall.window[i].tool_signature);
    memset(&g_stall, 0, sizeof(g_stall));
    reset_read_freq();
}

void stall_detector_record_turn(const char *tools_used, int output_chars, bool files_changed) {
    if (g_stall.window_count >= STALL_WINDOW_SIZE) {
        free(g_stall.window[0].tool_signature);
        memmove(&g_stall.window[0], &g_stall.window[1],
                (STALL_WINDOW_SIZE - 1) * sizeof(g_stall.window[0]));
        g_stall.window_count = STALL_WINDOW_SIZE - 1;
    }

    stall_turn_t *t = &g_stall.window[g_stall.window_count++];
    t->tool_signature = tools_used ? nc_xstrdup(tools_used) : NULL;
    t->output_chars = output_chars;
    t->files_changed = files_changed;
    t->timestamp = nc_wall_sec();
    g_stall.total_turns++;

    if (files_changed || output_chars > STALL_PROGRESS_OUTPUT_THRESHOLD)
        g_stall.last_progress_time = t->timestamp;
}

static bool signatures_identical(void) {
    if (g_stall.window_count < STALL_MIN_REPEAT)
        return false;
    const char *ref = g_stall.window[g_stall.window_count - 1].tool_signature;
    if (!ref)
        return false;
    int matches = 0;
    for (int i = g_stall.window_count - 2; i >= 0 && matches < STALL_MIN_REPEAT - 1; i--) {
        if (g_stall.window[i].tool_signature && strcmp(g_stall.window[i].tool_signature, ref) == 0)
            matches++;
        else
            break;
    }
    return matches >= STALL_MIN_REPEAT - 1;
}

static bool output_flat(void) {
    if (g_stall.window_count < STALL_MIN_REPEAT)
        return false;
    int lo = g_stall.window[g_stall.window_count - 1].output_chars;
    int hi = lo;
    for (int i = g_stall.window_count - 2;
         i >= 0 && (g_stall.window_count - 1 - i) < STALL_MIN_REPEAT; i--) {
        int v = g_stall.window[i].output_chars;
        if (v < lo)
            lo = v;
        if (v > hi)
            hi = v;
    }
    return (hi - lo) < STALL_OUTPUT_VARIANCE_FLOOR;
}

stall_verdict_t stall_detector_check(void) {
    if (g_stall.window_count < STALL_MIN_REPEAT)
        return STALL_OK;

    long now = nc_wall_sec();
    long idle_s = now - g_stall.last_progress_time;

    if (idle_s >= STALL_WALL_TIMEOUT_S)
        return STALL_TIMEOUT;

    if (signatures_identical() && output_flat())
        return STALL_LOOP_DETECTED;

    /* Only flag NO_PROGRESS when recent turns ALSO produced very little
     * output.  Read-only tasks (recon, analysis, web scraping) legitimately
     * produce large outputs without changing files — that is progress. */
    bool no_files = true;
    bool low_output = true;
    for (int i = g_stall.window_count - 1;
         i >= 0 && (g_stall.window_count - 1 - i) < STALL_MIN_REPEAT; i--) {
        if (g_stall.window[i].files_changed)
            no_files = false;
        if (g_stall.window[i].output_chars > STALL_PROGRESS_OUTPUT_THRESHOLD)
            low_output = false;
    }
    if (no_files && low_output && idle_s >= STALL_IDLE_THRESHOLD_S)
        return STALL_NO_PROGRESS;

    return STALL_OK;
}

const char *stall_verdict_name(stall_verdict_t v) {
    switch (v) {
    case STALL_OK:
        return "ok";
    case STALL_LOOP_DETECTED:
        return "loop_detected";
    case STALL_NO_PROGRESS:
        return "no_progress";
    case STALL_TIMEOUT:
        return "timeout";
    }
    return "unknown";
}

char *stall_detector_suggest(stall_verdict_t v) {
    strbuf_t sb;
    strbuf_init(&sb);

    switch (v) {
    case STALL_LOOP_DETECTED:
        strbuf_append_str(&sb, "[Stall] Repeated identical tool calls detected. "
                               "Try a different approach or compact context to reset state.");
        break;
    case STALL_NO_PROGRESS:
        strbuf_append_str(
            &sb, "[Stall] No file changes in recent turns. "
                 "Consider re-reading the goal, checking errors, or requesting a handoff.");
        break;
    case STALL_TIMEOUT:
        strbuf_appendf(&sb,
                       "[Stall] No meaningful progress for %ds. "
                       "Forcing context handoff to break the deadlock.",
                       STALL_WALL_TIMEOUT_S);
        break;
    default:
        strbuf_append_str(&sb, "No stall detected.");
        break;
    }
    return strbuf_detach(&sb);
}

/* ── Read-loop detection ── */

typedef struct {
    unsigned long hash;
    int count;
} read_freq_t;

static read_freq_t g_read_freq[STALL_READ_LOOP_WINDOW];
static int g_read_freq_count;

static void reset_read_freq(void) {
    g_read_freq_count = 0;
    memset(g_read_freq, 0, sizeof(g_read_freq));
}

static unsigned long djb2_hash(const char *s) {
    unsigned long h = NC_DJB2_SEED;
    int c;
    while ((c = (unsigned char)*s++))
        h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

void stall_detector_record_read(const char *path) {
    if (!path || !*path)
        return;
    unsigned long h = djb2_hash(path);
    for (int i = 0; i < g_read_freq_count; i++) {
        if (g_read_freq[i].hash == h) {
            g_read_freq[i].count++;
            return;
        }
    }
    if (g_read_freq_count < STALL_READ_LOOP_WINDOW) {
        g_read_freq[g_read_freq_count].hash = h;
        g_read_freq[g_read_freq_count].count = 1;
        g_read_freq_count++;
    }
}

bool stall_detector_read_loop(void) {
    for (int i = 0; i < g_read_freq_count; i++) {
        if (g_read_freq[i].count >= STALL_READ_LOOP_THRESHOLD)
            return true;
    }
    return false;
}

void stall_detector_reset_reads(void) {
    reset_read_freq();
}

long stall_detector_idle_s(void) {
    return nc_wall_sec() - g_stall.last_progress_time;
}

int stall_detector_window_count(void) {
    return g_stall.window_count;
}

void stall_detector_snapshot(stall_snapshot_t *out) {
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    long now_s = nc_wall_sec();
    out->window_count = g_stall.window_count;
    out->total_turns = g_stall.total_turns;
    out->last_progress_age_s = now_s - g_stall.last_progress_time;
    if (out->last_progress_age_s < 0)
        out->last_progress_age_s = 0;

    for (int i = 0; i < g_stall.window_count && i < STALL_WINDOW_SIZE; i++) {
        if (g_stall.window[i].tool_signature)
            nc_strlcpy(out->window[i].tool_signature, g_stall.window[i].tool_signature,
                       sizeof(out->window[i].tool_signature));
        out->window[i].output_chars = g_stall.window[i].output_chars;
        out->window[i].files_changed = g_stall.window[i].files_changed;
        out->window[i].age_s = now_s - g_stall.window[i].timestamp;
        if (out->window[i].age_s < 0)
            out->window[i].age_s = 0;
    }

    out->read_freq_count = g_read_freq_count;
    if (out->read_freq_count > STALL_READ_LOOP_WINDOW)
        out->read_freq_count = STALL_READ_LOOP_WINDOW;
    for (int i = 0; i < out->read_freq_count; i++) {
        out->reads[i].hash = g_read_freq[i].hash;
        out->reads[i].count = g_read_freq[i].count;
    }
}

void stall_detector_restore(const stall_snapshot_t *snap) {
    stall_detector_free();
    if (!snap) {
        stall_detector_init();
        return;
    }

    long now_s = nc_wall_sec();
    g_stall.window_count = snap->window_count;
    if (g_stall.window_count < 0)
        g_stall.window_count = 0;
    if (g_stall.window_count > STALL_WINDOW_SIZE)
        g_stall.window_count = STALL_WINDOW_SIZE;

    g_stall.total_turns = snap->total_turns;
    if (g_stall.total_turns < g_stall.window_count)
        g_stall.total_turns = g_stall.window_count;

    g_stall.last_progress_time = now_s - snap->last_progress_age_s;
    if (g_stall.last_progress_time > now_s)
        g_stall.last_progress_time = now_s;

    for (int i = 0; i < g_stall.window_count; i++) {
        if (snap->window[i].tool_signature[0])
            g_stall.window[i].tool_signature = nc_xstrdup(snap->window[i].tool_signature);
        g_stall.window[i].output_chars = snap->window[i].output_chars;
        g_stall.window[i].files_changed = snap->window[i].files_changed;
        g_stall.window[i].timestamp = now_s - snap->window[i].age_s;
        if (g_stall.window[i].timestamp > now_s)
            g_stall.window[i].timestamp = now_s;
    }

    g_read_freq_count = snap->read_freq_count;
    if (g_read_freq_count < 0)
        g_read_freq_count = 0;
    if (g_read_freq_count > STALL_READ_LOOP_WINDOW)
        g_read_freq_count = STALL_READ_LOOP_WINDOW;
    for (int i = 0; i < g_read_freq_count; i++) {
        g_read_freq[i].hash = snap->reads[i].hash;
        g_read_freq[i].count = snap->reads[i].count;
        if (g_read_freq[i].count < 0)
            g_read_freq[i].count = 0;
    }
}
