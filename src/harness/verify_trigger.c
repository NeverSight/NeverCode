/*
 * verify_trigger.c — Auto-verification trigger for non-trivial changes.
 *
 * Tracks file-edit counts per turn. When the number of distinct files
 * modified in a session exceeds a threshold, flags the turn for
 * independent verification (spawn a verification agent or prompt
 * the user). Prevents silent regressions in long-horizon tasks.
 */

#include "nevercode.h"

typedef struct {
    char path[VTRIG_PATH_MAX];
} vtrig_file_t;

static struct {
    vtrig_file_t files[VTRIG_MAX_FILES];
    int count;
    int threshold;
    bool triggered;
    int total_edits;
} g_vt;

void vtrig_init(void) {
    memset(&g_vt, 0, sizeof(g_vt));
    g_vt.threshold = VTRIG_DEFAULT_THRESHOLD;
}

void vtrig_set_threshold(int n) {
    if (n > 0)
        g_vt.threshold = n;
}

int vtrig_threshold(void) {
    return g_vt.threshold;
}

void vtrig_record_edit(const char *path) {
    if (!path || !*path)
        return;
    g_vt.total_edits++;
    for (int i = 0; i < g_vt.count; i++) {
        if (strcmp(g_vt.files[i].path, path) == 0)
            return;
    }
    if (g_vt.count < VTRIG_MAX_FILES) {
        nc_strlcpy(g_vt.files[g_vt.count].path, path, VTRIG_PATH_MAX);
        g_vt.count++;
    }
    if (g_vt.count >= g_vt.threshold && !g_vt.triggered)
        g_vt.triggered = true;
}

bool vtrig_should_verify(void) {
    return g_vt.triggered;
}

int vtrig_distinct_files(void) {
    return g_vt.count;
}

int vtrig_total_edits(void) {
    return g_vt.total_edits;
}

void vtrig_acknowledge(void) {
    g_vt.triggered = false;
}

void vtrig_reset(void) {
    memset(&g_vt, 0, sizeof(g_vt));
    g_vt.threshold = VTRIG_DEFAULT_THRESHOLD;
}

char *vtrig_summary(void) {
    if (g_vt.count == 0)
        return nc_xstrdup("No file edits recorded.");
    char buf[VTRIG_MAX_FILES * (VTRIG_PATH_MAX + 4) + 128];
    int pos = snprintf(buf, sizeof(buf), "%d distinct files edited (%d total edits):\n", g_vt.count,
                       g_vt.total_edits);
    for (int i = 0; i < g_vt.count && pos < (int)sizeof(buf) - VTRIG_PATH_MAX - 8; i++)
        pos += snprintf(buf + pos, sizeof(buf) - pos, "  %s\n", g_vt.files[i].path);
    return nc_xstrdup(buf);
}
