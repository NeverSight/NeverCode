/*
 * goal_tracker.c — Goal evolution and tracking
 *
 * Tracks a stack of goals for long-running sessions.  When a goal
 * completes, the next pending goal is automatically promoted.
 */

#include "harness.h"

const char *goal_status_name(goal_status_t s) {
    switch (s) {
    case GOAL_PENDING:
        return "pending";
    case GOAL_ACTIVE:
        return "active";
    case GOAL_COMPLETED:
        return "completed";
    case GOAL_FAILED:
        return "failed";
    case GOAL_EVOLVED:
        return "evolved";
    default:
        return "unknown";
    }
}

void goal_tracker_init(goal_tracker_t *g) {
    memset(g, 0, sizeof(*g));
    g->active_idx = -1;
}

void goal_tracker_free(goal_tracker_t *g) {
    for (int i = 0; i < g->count; i++) {
        free(g->goals[i].description);
        free(g->goals[i].result);
    }
    memset(g, 0, sizeof(*g));
    g->active_idx = -1;
}

int goal_tracker_push(goal_tracker_t *g, const char *description, int priority) {
    if (g->count >= HARNESS_MAX_GOAL_DEPTH)
        return -1;

    goal_entry_t *e = &g->goals[g->count];
    memset(e, 0, sizeof(*e));
    e->description = nc_xstrdup(description);
    e->status = GOAL_PENDING;
    e->priority = priority;
    e->created_at = nc_wall_sec();

    if (g->active_idx < 0) {
        e->status = GOAL_ACTIVE;
        g->active_idx = g->count;
    }

    g->count++;
    return g->count - 1;
}

static void activate_next(goal_tracker_t *g) {
    g->active_idx = -1;
    for (int i = 0; i < g->count; i++) {
        if (g->goals[i].status == GOAL_PENDING) {
            g->goals[i].status = GOAL_ACTIVE;
            g->active_idx = i;
            break;
        }
    }
}

int goal_tracker_complete(goal_tracker_t *g, int idx, const char *result) {
    if (idx < 0 || idx >= g->count)
        return -1;

    goal_entry_t *e = &g->goals[idx];
    e->status = GOAL_COMPLETED;
    e->completed_at = nc_wall_sec();
    free(e->result);
    e->result = result ? nc_xstrdup(result) : NULL;

    if (g->active_idx == idx)
        activate_next(g);
    return 0;
}

int goal_tracker_fail(goal_tracker_t *g, int idx, const char *reason) {
    if (idx < 0 || idx >= g->count)
        return -1;

    goal_entry_t *e = &g->goals[idx];
    e->status = GOAL_FAILED;
    e->completed_at = nc_wall_sec();
    free(e->result);
    e->result = reason ? nc_xstrdup(reason) : NULL;

    if (g->active_idx == idx)
        activate_next(g);
    return 0;
}

const char *goal_tracker_current(const goal_tracker_t *g) {
    if (g->active_idx < 0 || g->active_idx >= g->count)
        return NULL;
    return g->goals[g->active_idx].description;
}

int goal_tracker_evolve(goal_tracker_t *g, int idx, const char **sub_goals, int n_sub) {
    if (idx < 0 || idx >= g->count || !sub_goals || n_sub <= 0)
        return -1;
    if (g->count + n_sub > HARNESS_MAX_GOAL_DEPTH)
        return -1;

    goal_entry_t *parent = &g->goals[idx];
    int base_priority = parent->priority;
    bool was_active = (g->active_idx == idx);

    parent->status = GOAL_EVOLVED;
    parent->completed_at = nc_wall_sec();
    free(parent->result);
    parent->result = nc_xstrdup("evolved into sub-goals");

    for (int i = 0; i < n_sub; i++) {
        goal_entry_t *e = &g->goals[g->count];
        memset(e, 0, sizeof(*e));
        e->description = nc_xstrdup(sub_goals[i]);
        e->status = GOAL_PENDING;
        e->priority = base_priority + i;
        e->created_at = nc_wall_sec();
        g->count++;
    }

    if (was_active)
        activate_next(g);

    return 0;
}

int goal_tracker_active_idx(const goal_tracker_t *g) {
    return g->active_idx;
}

int goal_tracker_count(const goal_tracker_t *g) {
    return g->count;
}

int goal_tracker_pending_count(const goal_tracker_t *g) {
    int n = 0;
    for (int i = 0; i < g->count; i++)
        if (g->goals[i].status == GOAL_PENDING || g->goals[i].status == GOAL_ACTIVE)
            n++;
    return n;
}

int goal_tracker_completed_count(const goal_tracker_t *g) {
    int n = 0;
    for (int i = 0; i < g->count; i++)
        if (g->goals[i].status == GOAL_COMPLETED)
            n++;
    return n;
}

char *goal_tracker_format_status(const goal_tracker_t *g) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_appendf(&sb, "Goals: %d total, %d pending, %d completed\n", g->count,
                   goal_tracker_pending_count(g), goal_tracker_completed_count(g));

    for (int i = 0; i < g->count; i++) {
        const goal_entry_t *e = &g->goals[i];
        const char *marker;
        switch (e->status) {
        case GOAL_ACTIVE:
            marker = ">>";
            break;
        case GOAL_COMPLETED:
            marker = "OK";
            break;
        case GOAL_FAILED:
            marker = "!!";
            break;
        case GOAL_EVOLVED:
            marker = "->";
            break;
        default:
            marker = "  ";
            break;
        }
        strbuf_appendf(&sb, "  [%s] %s (priority=%d)\n", marker,
                       e->description ? e->description : "(none)", e->priority);
        if (e->result)
            strbuf_appendf(&sb, "       result: %s\n", e->result);
    }

    return strbuf_detach(&sb);
}
