/*
 * swarm.c — multi-agent work queue coordinator
 */

#include "defaults.h"
#include "nevercode.h"

enum { W_PENDING = 0, W_ACTIVE, W_DONE, W_FAILED };

typedef struct {
    char *desc;
    int priority;
    int state;
    char *agent;
    char *result;
    int retries;
} swarm_item_t;

static swarm_item_t g_items[SWARM_MAX_WORK_ITEMS];
static int g_nitems;
static int g_max_agents;
static char *g_goal;
static time_t g_deadline;
static swarm_phase_t g_phase;

static void item_clear(swarm_item_t *it) {
    free(it->desc);
    free(it->agent);
    free(it->result);
    memset(it, 0, sizeof(*it));
}

void swarm_init(int max_agents) {
    swarm_free();
    g_max_agents = max_agents > 0 ? max_agents : SWARM_DEFAULT_AGENTS;
    if (g_max_agents > SWARM_MAX_AGENTS)
        g_max_agents = SWARM_MAX_AGENTS;
    g_phase = SWARM_IDLE;
}

void swarm_free(void) {
    for (int i = 0; i < g_nitems; i++)
        item_clear(&g_items[i]);
    g_nitems = 0;
    free(g_goal);
    g_goal = NULL;
    g_deadline = 0;
    g_phase = SWARM_IDLE;
}

int swarm_begin(const char *goal, long deadline_s) {
    if (!goal)
        return -1;
    for (int i = 0; i < g_nitems; i++)
        item_clear(&g_items[i]);
    g_nitems = 0;
    free(g_goal);
    g_goal = nc_xstrdup(goal);
    if (!g_goal)
        return -1;
    g_deadline = deadline_s > 0 ? nc_time_now() + (time_t)deadline_s : 0;
    g_phase = SWARM_ACTIVE;
    return 0;
}

int swarm_add_work(const char *description, int priority) {
    if (!description || g_nitems >= SWARM_MAX_WORK_ITEMS)
        return -1;
    swarm_item_t *w = &g_items[g_nitems];
    w->desc = nc_xstrdup(description);
    w->priority = priority;
    w->state = W_PENDING;
    if (!w->desc)
        return -1;
    g_nitems++;
    return 0;
}

int swarm_assign_next(const char *agent_id) {
    if (!agent_id || g_phase != SWARM_ACTIVE)
        return -1;
    int best = -1;
    for (int i = 0; i < g_nitems; i++) {
        if (g_items[i].state != W_PENDING)
            continue;
        if (best < 0 || g_items[i].priority < g_items[best].priority)
            best = i;
    }
    if (best < 0)
        return -1;
    free(g_items[best].agent);
    g_items[best].agent = nc_xstrdup(agent_id);
    if (!g_items[best].agent)
        return -1;
    g_items[best].state = W_ACTIVE;
    return best + 1;
}

int swarm_complete_work(int idx, const char *result, bool success) {
    int i = idx - 1;
    if (i < 0 || i >= g_nitems)
        return -1;
    free(g_items[i].result);
    g_items[i].result = result ? nc_xstrdup(result) : NULL;
    g_items[i].state = success ? W_DONE : W_FAILED;
    return 0;
}

void swarm_end(void) {
    g_phase = SWARM_IDLE;
}

swarm_phase_t swarm_phase(void) {
    return g_phase;
}

int swarm_pending_count(void) {
    int c = 0;
    for (int i = 0; i < g_nitems; i++)
        if (g_items[i].state == W_PENDING)
            c++;
    return c;
}

int swarm_completed_count(void) {
    int c = 0;
    for (int i = 0; i < g_nitems; i++)
        if (g_items[i].state == W_DONE)
            c++;
    return c;
}

int swarm_failed_count(void) {
    int c = 0;
    for (int i = 0; i < g_nitems; i++)
        if (g_items[i].state == W_FAILED)
            c++;
    return c;
}

bool swarm_timed_out(void) {
    if (g_deadline == 0)
        return false;
    return nc_time_now() > g_deadline;
}

const char *swarm_phase_name(swarm_phase_t p) {
    switch (p) {
    case SWARM_IDLE:
        return "idle";
    case SWARM_ACTIVE:
        return "active";
    case SWARM_CONVERGING:
        return "converging";
    case SWARM_COMPLETE:
        return "complete";
    case SWARM_ABORTED:
        return "aborted";
    default:
        return "unknown";
    }
}

char *swarm_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "swarm: phase=%s items=%d pending=%d done=%d fail=%d agents=%d",
                   swarm_phase_name(g_phase), g_nitems, swarm_pending_count(),
                   swarm_completed_count(), swarm_failed_count(), g_max_agents);
    if (g_goal)
        strbuf_appendf(&sb, " goal=\"%s\"", g_goal);
    if (swarm_timed_out())
        strbuf_append_str(&sb, " [timed_out]");
    return strbuf_detach(&sb);
}
