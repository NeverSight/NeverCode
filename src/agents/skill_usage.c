/*
 * skill_usage.c — Skill/command usage frequency tracking with recency decay.
 *
 * Records how often and how recently each skill is invoked.  The
 * scoring function uses exponential decay with a configurable
 * half-life so recently used skills rank higher in suggestions.
 */

#include "nevercode.h"
#include <math.h>

typedef struct {
    char name[SKILL_USAGE_NAME_MAX];
    int count;
    long last_used;
} su_entry_t;

static su_entry_t g_entries[SKILL_USAGE_MAX];
static int g_count = 0;

void skill_usage_record(const char *name) {
    if (!name || !name[0])
        return;

    long now = nc_wall_sec();

    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].name, name) == 0) {
            g_entries[i].count++;
            g_entries[i].last_used = now;
            return;
        }
    }

    if (g_count >= SKILL_USAGE_MAX) {
        /* Evict lowest-scoring entry */
        int worst = 0;
        double worst_score = skill_usage_score(g_entries[0].name);
        for (int i = 1; i < g_count; i++) {
            double s = skill_usage_score(g_entries[i].name);
            if (s < worst_score) {
                worst_score = s;
                worst = i;
            }
        }
        g_entries[worst].count = 1;
        g_entries[worst].last_used = now;
        nc_strlcpy(g_entries[worst].name, name, sizeof(g_entries[worst].name));
        return;
    }

    su_entry_t *e = &g_entries[g_count++];
    nc_strlcpy(e->name, name, sizeof(e->name));
    e->count = 1;
    e->last_used = now;
}

double skill_usage_score(const char *name) {
    if (!name)
        return 0.0;

    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].name, name) == 0) {
            long now = nc_wall_sec();
            double days_since = (double)(now - g_entries[i].last_used) / 86400.0;
            double decay = pow(0.5, days_since / SKILL_USAGE_HALFLIFE_DAYS);
            return (double)g_entries[i].count * decay;
        }
    }
    return 0.0;
}

int skill_usage_count(const char *name) {
    if (!name)
        return 0;
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].name, name) == 0)
            return g_entries[i].count;
    }
    return 0;
}

int skill_usage_total(void) {
    return g_count;
}

void skill_usage_reset(void) {
    g_count = 0;
}
