/*
 * startup_profiler.c — Init-phase timing for performance analysis.
 *
 * Records named checkpoints during startup. Reports cumulative
 * and per-phase durations so slow init stages are visible.
 * Enabled via NEVERCODE_PROFILE_STARTUP=1 environment variable.
 */

#include "nevercode.h"

typedef struct {
    char name[SPROF_NAME_MAX];
    long ts_us;
} sprof_mark_t;

static struct {
    sprof_mark_t marks[SPROF_MAX_MARKS];
    int count;
    long origin_us;
    bool enabled;
} g_sp;

#define now_us nc_mono_us

void sprof_init(void) {
    memset(&g_sp, 0, sizeof(g_sp));
    const char *env = getenv(SPROF_ENV_VAR);
    g_sp.enabled = (env && (*env == '1' || *env == 'y' || *env == 'Y'));
    g_sp.origin_us = now_us();
}

void sprof_checkpoint(const char *name) {
    if (!g_sp.enabled || !name || g_sp.count >= SPROF_MAX_MARKS)
        return;
    sprof_mark_t *m = &g_sp.marks[g_sp.count++];
    nc_strlcpy(m->name, name, SPROF_NAME_MAX);
    m->ts_us = now_us();
}

int sprof_count(void) {
    return g_sp.count;
}

bool sprof_enabled(void) {
    return g_sp.enabled;
}

void sprof_set_enabled(bool on) {
    g_sp.enabled = on;
    if (on && g_sp.origin_us == 0)
        g_sp.origin_us = now_us();
}

char *sprof_report(void) {
    if (g_sp.count == 0)
        return nc_xstrdup("(no checkpoints recorded)");

    char buf[SPROF_MAX_MARKS * (SPROF_NAME_MAX + 64) + 128];
    int pos = snprintf(buf, sizeof(buf), "Startup profile (%d checkpoints):\n", g_sp.count);

    for (int i = 0; i < g_sp.count && pos < (int)sizeof(buf) - 120; i++) {
        long abs_ms = (g_sp.marks[i].ts_us - g_sp.origin_us) / 1000;
        long delta_ms = (i == 0) ? abs_ms : (g_sp.marks[i].ts_us - g_sp.marks[i - 1].ts_us) / 1000;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "  [%4ld ms +%3ld ms] %s\n", abs_ms, delta_ms,
                        g_sp.marks[i].name);
    }

    long total_ms = (g_sp.marks[g_sp.count - 1].ts_us - g_sp.origin_us) / 1000;
    snprintf(buf + pos, sizeof(buf) - pos, "  Total: %ld ms\n", total_ms);
    return nc_xstrdup(buf);
}

long sprof_total_ms(void) {
    if (g_sp.count == 0)
        return 0;
    return (g_sp.marks[g_sp.count - 1].ts_us - g_sp.origin_us) / 1000;
}
