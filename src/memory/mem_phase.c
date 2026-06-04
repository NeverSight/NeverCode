/*
 * mem_phase.c — Two-phase memory extraction pipeline
 *
 * Phase 1: Extract raw memories from rollout data (per-session).
 * Phase 2: Consolidate and deduplicate across sessions.
 * Multi-stage memory lifecycle management.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Internal types ── */

typedef struct {
    char content[MPL_PHASE1_SUMMARY_TOKENS * 4];
    char session_id[PROTO_ID_LEN];
    char slug[TOOL_POOL_NAME_MAX];
    mem_phase_t phase;
    long created_at;
    int token_estimate;
    bool pruned;
} phase_memory_t;

typedef struct {
    phase_memory_t items[MPL_MAX_RAW_MEMORIES];
    int count;
    int phase1_done;
    int phase2_done;
    bool running;
} mem_phase_ctx_t;

static mem_phase_ctx_t g_phases;

/* ── Init / Free ── */

void mem_phase_init(void) {
    memset(&g_phases, 0, sizeof(g_phases));
}

void mem_phase_free(void) {
    g_phases.count = 0;
    g_phases.running = false;
}

/* ── Phase 1: Extraction ── */

int mem_phase1_add_raw(const char *content, const char *session_id, int tokens) {
    if (!content || g_phases.count >= MPL_MAX_RAW_MEMORIES)
        return -1;
    if (tokens < MPL_PHASE1_MIN_TOKENS)
        return -1;

    phase_memory_t *m = &g_phases.items[g_phases.count];
    nc_strlcpy(m->content, content, sizeof(m->content));
    if (session_id)
        nc_strlcpy(m->session_id, session_id, sizeof(m->session_id));
    m->phase = MEM_PHASE_RAW;
    m->created_at = nc_wall_sec();
    m->token_estimate = tokens;
    g_phases.count++;
    return g_phases.count - 1;
}

int mem_phase1_extract(int idx) {
    if (idx < 0 || idx >= g_phases.count)
        return -1;
    phase_memory_t *m = &g_phases.items[idx];
    if (m->phase != MEM_PHASE_RAW)
        return -1;

    /* Generate slug from first N chars */
    const char *src = m->content;
    int slen = 0;
    for (int i = 0; i < 32 && src[i] && src[i] != '\n'; i++) {
        char c = src[i];
        if (c == ' ')
            c = '_';
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            m->slug[slen++] = c;
    }
    m->slug[slen] = '\0';

    m->phase = MEM_PHASE_EXTRACTED;
    g_phases.phase1_done++;
    return 0;
}

int mem_phase1_run_all(void) {
    g_phases.running = true;
    int extracted = 0;
    for (int i = 0; i < g_phases.count; i++) {
        if (g_phases.items[i].phase == MEM_PHASE_RAW) {
            if (mem_phase1_extract(i) == 0)
                extracted++;
        }
    }
    g_phases.running = false;
    return extracted;
}

/* ── Phase 2: Consolidation ── */

static bool memories_similar(const phase_memory_t *a, const phase_memory_t *b) {
    if (a->slug[0] && b->slug[0] && strcmp(a->slug, b->slug) == 0)
        return true;

    /* Simple content overlap check */
    int alen = (int)strlen(a->content);
    int blen = (int)strlen(b->content);
    int min_len = alen < blen ? alen : blen;
    if (min_len < 20)
        return false;

    int matches = 0;
    for (int i = 0; i < min_len && i < MPHASE_CMP_WINDOW; i++)
        if (a->content[i] == b->content[i])
            matches++;

    return (matches * 100 / (min_len < MPHASE_CMP_WINDOW ? min_len : MPHASE_CMP_WINDOW)) >
           MPHASE_SIMILARITY_THRESHOLD;
}

int mem_phase2_consolidate(void) {
    g_phases.running = true;
    int merged = 0;

    for (int i = 0; i < g_phases.count; i++) {
        if (g_phases.items[i].phase != MEM_PHASE_EXTRACTED || g_phases.items[i].pruned)
            continue;

        for (int j = i + 1; j < g_phases.count; j++) {
            if (g_phases.items[j].phase != MEM_PHASE_EXTRACTED || g_phases.items[j].pruned)
                continue;

            if (memories_similar(&g_phases.items[i], &g_phases.items[j])) {
                g_phases.items[j].pruned = true;
                merged++;
            }
        }

        g_phases.items[i].phase = MEM_PHASE_MERGED;
        g_phases.phase2_done++;
    }

    g_phases.running = false;
    return merged;
}

/* ── Queries ── */

int mem_phase_count(void) {
    return g_phases.count;
}
int mem_phase_raw_count(void) {
    int c = 0;
    for (int i = 0; i < g_phases.count; i++)
        if (g_phases.items[i].phase == MEM_PHASE_RAW && !g_phases.items[i].pruned)
            c++;
    return c;
}
int mem_phase_extracted_count(void) {
    return g_phases.phase1_done;
}
bool mem_phase_is_running(void) {
    return g_phases.running;
}

int mem_phase_active_count(void) {
    int c = 0;
    for (int i = 0; i < g_phases.count; i++)
        if (!g_phases.items[i].pruned && g_phases.items[i].phase != MEM_PHASE_ARCHIVED)
            c++;
    return c;
}

char *mem_phase_format_status(void) {
    size_t sz = 512;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;
    snprintf(buf, sz,
             "Memory phases: %d total, %d raw, %d extracted, %d merged, %d active\n"
             "Phase 1 done: %d | Phase 2 done: %d | Running: %s\n",
             g_phases.count, mem_phase_raw_count(), g_phases.phase1_done, g_phases.phase2_done,
             mem_phase_active_count(), g_phases.phase1_done, g_phases.phase2_done,
             g_phases.running ? "yes" : "no");
    return buf;
}
