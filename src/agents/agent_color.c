/*
 * agent_color.c — Deterministic color assignment for agent types
 *
 * Maps agent type names to ANSI color codes via a stable hash,
 * ensuring the same agent type always renders in the same color
 * across sessions and terminals.
 */

#include "nevercode.h"
#include "terminal.h"

static const char *COLORS[] = {
    T_CYAN, T_YELLOW, T_MAGENTA, T_GREEN, T_BLUE, T_BRIGHT_RED, T_BRIGHT_YELLOW, T_BRIGHT_MAGENTA,
};

typedef struct {
    char type[NC_AGENT_TYPE_MAX];
    int color_idx;
} color_slot_t;

static color_slot_t g_slots[MAX_LIVE_AGENTS];
static int g_slot_count = 0;

static unsigned int hash_name(const char *s) {
    unsigned int h = NC_DJB2_SEED;
    for (; *s; s++)
        h = ((h << 5) + h) ^ (unsigned char)*s;
    return h;
}

const char *agent_color_for(const char *agent_type) {
    if (!agent_type || !agent_type[0])
        return COLORS[0];

    for (int i = 0; i < g_slot_count; i++) {
        if (strcmp(g_slots[i].type, agent_type) == 0)
            return COLORS[g_slots[i].color_idx % NC_AGENT_COLOR_COUNT];
    }

    int idx = (int)(hash_name(agent_type) % NC_AGENT_COLOR_COUNT);
    if (g_slot_count < MAX_LIVE_AGENTS) {
        nc_strlcpy(g_slots[g_slot_count].type, agent_type, sizeof(g_slots[0].type));
        g_slots[g_slot_count].color_idx = idx;
        g_slot_count++;
    }
    return COLORS[idx % NC_AGENT_COLOR_COUNT];
}

void agent_color_reset(void) {
    g_slot_count = 0;
}
