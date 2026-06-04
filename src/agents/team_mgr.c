/*
 * team_mgr.c — Multi-agent team lifecycle management
 *
 * Creates named teams with goals, adds/removes agent members,
 * and tracks active/disbanded status for collaborative workflows.
 */

#include "nevercode.h"

typedef struct {
    char agent_id[NC_FMT_MAX];
    char role[NC_FMT_MAX];
} team_member_t;

typedef struct {
    char name[NC_FMT_MAX];
    char goal[NC_LABEL_MAX];
    team_member_t members[TEAM_MAX_MEMBERS];
    int member_count;
    bool active;
    long created_at;
} team_t;

static struct {
    team_t teams[TEAM_MAX_TEAMS];
    int count;
    bool initialized;
} g_teams;

void team_mgr_init(void) {
    memset(&g_teams, 0, sizeof(g_teams));
    g_teams.initialized = true;
}

void team_mgr_free(void) {
    memset(&g_teams, 0, sizeof(g_teams));
}

int team_create(const char *name, const char *goal) {
    if (!g_teams.initialized || g_teams.count >= TEAM_MAX_TEAMS)
        return -1;

    team_t *t = &g_teams.teams[g_teams.count];
    nc_strlcpy(t->name, name ? name : "team", sizeof(t->name));
    nc_strlcpy(t->goal, goal ? goal : "", sizeof(t->goal));
    t->member_count = 0;
    t->active = true;
    t->created_at = nc_wall_sec();
    g_teams.count++;
    return g_teams.count - 1;
}

int team_add_member(int team_id, const char *agent_id, const char *role) {
    if (team_id < 0 || team_id >= g_teams.count || !agent_id)
        return -1;

    team_t *t = &g_teams.teams[team_id];
    if (t->member_count >= TEAM_MAX_MEMBERS)
        return -1;

    team_member_t *m = &t->members[t->member_count];
    nc_strlcpy(m->agent_id, agent_id, sizeof(m->agent_id));
    nc_strlcpy(m->role, role ? role : "worker", sizeof(m->role));
    t->member_count++;
    return 0;
}

int team_remove_member(int team_id, const char *agent_id) {
    if (team_id < 0 || team_id >= g_teams.count || !agent_id)
        return -1;

    team_t *t = &g_teams.teams[team_id];
    for (int i = 0; i < t->member_count; i++) {
        if (strcmp(t->members[i].agent_id, agent_id) == 0) {
            memmove(&t->members[i], &t->members[i + 1],
                    (size_t)(t->member_count - i - 1) * sizeof(team_member_t));
            t->member_count--;
            return 0;
        }
    }
    return -1;
}

int team_disband(int team_id) {
    if (team_id < 0 || team_id >= g_teams.count)
        return -1;
    g_teams.teams[team_id].active = false;
    return 0;
}

int team_count(void) {
    return g_teams.count;
}

int team_member_count(int team_id) {
    return (team_id >= 0 && team_id < g_teams.count) ? g_teams.teams[team_id].member_count : 0;
}

const char *team_name(int team_id) {
    return (team_id >= 0 && team_id < g_teams.count) ? g_teams.teams[team_id].name : NULL;
}

const char *team_goal(int team_id) {
    return (team_id >= 0 && team_id < g_teams.count) ? g_teams.teams[team_id].goal : NULL;
}

char *team_format_status(int team_id) {
    if (team_id < 0 || team_id >= g_teams.count)
        return NULL;

    team_t *t = &g_teams.teams[team_id];
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Team '%s' (%s): %d members\n  goal: %s\n", t->name,
                   t->active ? "active" : "disbanded", t->member_count, t->goal);

    for (int i = 0; i < t->member_count; i++)
        strbuf_appendf(&sb, "  - %s (%s)\n", t->members[i].agent_id, t->members[i].role);

    return strbuf_detach(&sb);
}

char *team_format_all(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Teams: %d\n", g_teams.count);

    for (int i = 0; i < g_teams.count; i++) {
        char *s = team_format_status(i);
        if (s) {
            strbuf_append_str(&sb, s);
            free(s);
        }
    }
    return strbuf_detach(&sb);
}
