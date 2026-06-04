/*
 * swarm_backend.c — Multi-agent spawn backend registry.
 *
 * Provides pluggable backends for spawning parallel agents:
 *   - in-process: fork within the same process
 *   - tmux: spawn in a new tmux pane
 *   - external: launch a separate nevercode process
 *
 * The registry auto-detects available backends and caches the selection.
 */

#include "nevercode.h"
#include "terminal.h"

/* BACKEND_MAX, BACKEND_NAME_MAX in defaults.h */

typedef enum { SB_IN_PROCESS = 0, SB_TMUX, SB_EXTERNAL, SB_NONE } sb_type_t;

typedef struct {
    sb_type_t type;
    char name[BACKEND_NAME_MAX];
    bool available;
    bool preferred;
    int spawned;
    int failed;
} sb_entry_t;

static struct {
    sb_entry_t backends[BACKEND_MAX];
    int count;
    sb_type_t active;
    bool detected;
    int total_spawned;
} g_sb;

static const char *_type_label(sb_type_t t) {
    switch (t) {
    case SB_IN_PROCESS:
        return "in-process";
    case SB_TMUX:
        return "tmux";
    case SB_EXTERNAL:
        return "external";
    case SB_NONE:
        return "none";
    }
    return "unknown";
}

static int _register(sb_type_t type, const char *name, bool avail, bool pref) {
    if (g_sb.count >= BACKEND_MAX)
        return -1;
    sb_entry_t *e = &g_sb.backends[g_sb.count++];
    e->type = type;
    nc_strlcpy(e->name, name, BACKEND_NAME_MAX);
    e->available = avail;
    e->preferred = pref;
    e->spawned = 0;
    e->failed = 0;
    return g_sb.count - 1;
}

void swarm_backend_init(void) {
    memset(&g_sb, 0, sizeof(g_sb));
    g_sb.active = SB_NONE;
}

void swarm_backend_free(void) {
    memset(&g_sb, 0, sizeof(g_sb));
}

int swarm_backend_detect(void) {
    if (g_sb.detected)
        return g_sb.count;

    g_sb.count = 0;

    _register(SB_IN_PROCESS, "in-process", true, false);

    bool tmux_avail = nc_cmd_exists("tmux");
    bool in_tmux = td_is_tmux();
    _register(SB_TMUX, "tmux", tmux_avail && in_tmux, tmux_avail && in_tmux);

    _register(SB_EXTERNAL, "external", true, false);

    for (int i = g_sb.count - 1; i >= 0; i--) {
        if (g_sb.backends[i].available && g_sb.backends[i].preferred) {
            g_sb.active = g_sb.backends[i].type;
            break;
        }
    }
    if (g_sb.active == SB_NONE) {
        for (int i = 0; i < g_sb.count; i++) {
            if (g_sb.backends[i].available) {
                g_sb.active = g_sb.backends[i].type;
                break;
            }
        }
    }

    g_sb.detected = true;
    return g_sb.count;
}

sb_type_t swarm_backend_active(void) {
    if (!g_sb.detected)
        swarm_backend_detect();
    return g_sb.active;
}

void swarm_backend_set_active(sb_type_t type) {
    g_sb.active = type;
}

static sb_entry_t *_find_entry(sb_type_t type) {
    for (int i = 0; i < g_sb.count; i++)
        if (g_sb.backends[i].type == type)
            return &g_sb.backends[i];
    return NULL;
}

int swarm_backend_spawn(const char *task, const char *model, int max_turns, char *agent_id_out,
                        size_t id_sz) {
    if (!g_sb.detected)
        swarm_backend_detect();
    sb_entry_t *be = _find_entry(g_sb.active);
    if (!be || !be->available)
        return -1;

    agent_pool_t *pool = agent_pool_global();
    if (!pool)
        return -1;

    char *aid = agent_pool_spawn(pool, task, _type_label(be->type), NULL);
    if (!aid) {
        be->failed++;
        return -1;
    }

    be->spawned++;
    g_sb.total_spawned++;
    if (agent_id_out && id_sz > 0)
        nc_strlcpy(agent_id_out, aid, id_sz);
    free(aid);
    (void)model;
    (void)max_turns;
    return 0;
}

int swarm_backend_count(void) {
    return g_sb.count;
}

int swarm_backend_available_count(void) {
    int n = 0;
    for (int i = 0; i < g_sb.count; i++)
        if (g_sb.backends[i].available)
            n++;
    return n;
}

int swarm_backend_total_spawned(void) {
    return g_sb.total_spawned;
}

const char *swarm_backend_active_name(void) {
    return _type_label(swarm_backend_active());
}

char *swarm_backend_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Swarm backends: active=%s total_spawned=%d\n", swarm_backend_active_name(),
                   g_sb.total_spawned);
    for (int i = 0; i < g_sb.count; i++) {
        sb_entry_t *e = &g_sb.backends[i];
        strbuf_appendf(&sb, "  %s: avail=%s pref=%s spawned=%d failed=%d\n", e->name,
                       e->available ? "yes" : "no", e->preferred ? "yes" : "no", e->spawned,
                       e->failed);
    }
    return strbuf_detach(&sb);
}
