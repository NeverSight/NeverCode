/*
 * agent_health.c — lightweight per-agent lifecycle / activity tracking
 */

#include "nevercode.h"

/* AGENT_HEALTH_MAX in defaults.h */

typedef struct {
    char *agent_id;
    int phase;
    int turn_count;
    int tool_count;
    int error_count;
    char *last_tool;
    char *progress_note;
    long last_activity_ms;
} ah_entry_t;

static struct {
    ah_entry_t e[AGENT_HEALTH_MAX];
    int n;
    int inited;
} g_ah;

static int find_idx(const char *agent_id) {
    if (!agent_id)
        return -1;
    for (int i = 0; i < g_ah.n; i++) {
        if (g_ah.e[i].agent_id && strcmp(g_ah.e[i].agent_id, agent_id) == 0)
            return i;
    }
    return -1;
}

static void entry_clear(ah_entry_t *x) {
    free(x->agent_id);
    free(x->last_tool);
    free(x->progress_note);
    memset(x, 0, sizeof(*x));
}

void agent_health_init(void) {
    if (g_ah.inited)
        return;
    memset(&g_ah, 0, sizeof(g_ah));
    g_ah.inited = 1;
}

void agent_health_free(void) {
    for (int i = 0; i < g_ah.n; i++)
        entry_clear(&g_ah.e[i]);
    g_ah.n = 0;
    g_ah.inited = 0;
}

int agent_health_register(const char *agent_id) {
    agent_health_init();
    if (!agent_id || !agent_id[0])
        return -1;
    if (find_idx(agent_id) >= 0)
        return 0;
    if (g_ah.n >= AGENT_HEALTH_MAX)
        return -1;
    ah_entry_t *x = &g_ah.e[g_ah.n];
    memset(x, 0, sizeof(*x));
    x->agent_id = nc_xstrdup(agent_id);
    if (!x->agent_id)
        return -1;
    x->last_activity_ms = nc_mono_ms();
    g_ah.n++;
    return 0;
}

int agent_health_set_phase(const char *agent_id, int phase) {
    int i = find_idx(agent_id);
    if (i < 0 && agent_health_register(agent_id) != 0)
        return -1;
    i = find_idx(agent_id);
    if (i < 0)
        return -1;
    g_ah.e[i].phase = phase;
    g_ah.e[i].last_activity_ms = nc_mono_ms();
    return 0;
}

int agent_health_record_turn(const char *agent_id) {
    int i = find_idx(agent_id);
    if (i < 0 && agent_health_register(agent_id) != 0)
        return -1;
    i = find_idx(agent_id);
    if (i < 0)
        return -1;
    g_ah.e[i].turn_count++;
    g_ah.e[i].last_activity_ms = nc_mono_ms();
    return 0;
}

int agent_health_record_tool(const char *agent_id, const char *tool_name) {
    int i = find_idx(agent_id);
    if (i < 0 && agent_health_register(agent_id) != 0)
        return -1;
    i = find_idx(agent_id);
    if (i < 0)
        return -1;
    g_ah.e[i].tool_count++;
    free(g_ah.e[i].last_tool);
    g_ah.e[i].last_tool = tool_name && tool_name[0] ? nc_xstrdup(tool_name) : NULL;
    g_ah.e[i].last_activity_ms = nc_mono_ms();
    return 0;
}

int agent_health_record_error(const char *agent_id) {
    int i = find_idx(agent_id);
    if (i < 0 && agent_health_register(agent_id) != 0)
        return -1;
    i = find_idx(agent_id);
    if (i < 0)
        return -1;
    g_ah.e[i].error_count++;
    g_ah.e[i].last_activity_ms = nc_mono_ms();
    return 0;
}

int agent_health_set_progress(const char *agent_id, const char *note) {
    int i = find_idx(agent_id);
    if (i < 0 && agent_health_register(agent_id) != 0)
        return -1;
    i = find_idx(agent_id);
    if (i < 0)
        return -1;
    free(g_ah.e[i].progress_note);
    g_ah.e[i].progress_note = note && note[0] ? nc_xstrdup(note) : NULL;
    g_ah.e[i].last_activity_ms = nc_mono_ms();
    return 0;
}

int agent_health_unregister(const char *agent_id) {
    int i = find_idx(agent_id);
    if (i < 0)
        return -1;
    entry_clear(&g_ah.e[i]);
    if (i < g_ah.n - 1)
        g_ah.e[i] = g_ah.e[g_ah.n - 1];
    memset(&g_ah.e[g_ah.n - 1], 0, sizeof(ah_entry_t));
    g_ah.n--;
    return 0;
}

long agent_health_idle_ms(const char *agent_id) {
    int i = find_idx(agent_id);
    if (i < 0)
        return -1;
    return nc_mono_ms() - g_ah.e[i].last_activity_ms;
}

int agent_health_count(void) {
    return g_ah.n;
}

char *agent_health_format(const char *agent_id) {
    int i = find_idx(agent_id);
    if (i < 0)
        return NULL;
    ah_entry_t *x = &g_ah.e[i];
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "agent=%s phase=%d turns=%d tools=%d errors=%d idle_ms=%ld", x->agent_id,
                   x->phase, x->turn_count, x->tool_count, x->error_count,
                   (long)(nc_mono_ms() - x->last_activity_ms));
    if (x->last_tool) {
        strbuf_append_str(&sb, " last_tool=");
        strbuf_append_str(&sb, x->last_tool);
    }
    if (x->progress_note) {
        strbuf_append_str(&sb, " note=");
        strbuf_append_str(&sb, x->progress_note);
    }
    return strbuf_detach(&sb);
}

char *agent_health_format_all(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    if (g_ah.n == 0) {
        strbuf_append_str(&sb, "(no agent health entries)");
        return strbuf_detach(&sb);
    }
    for (int i = 0; i < g_ah.n; i++) {
        char *one = agent_health_format(g_ah.e[i].agent_id);
        if (one) {
            strbuf_append_str(&sb, one);
            strbuf_append_str(&sb, "\n");
            free(one);
        }
    }
    return strbuf_detach(&sb);
}
