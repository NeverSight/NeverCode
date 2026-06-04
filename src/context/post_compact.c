/*
 * post_compact.c — Cleanup actions after context compaction
 *
 * Resets transient caches and tracking state that become stale after
 * compaction replaces the message list:
 *   - Approval cache (re-derived from config on next turn)
 *   - Prompt suggestions (session context changed)
 *   - Prompt section cache (system prompt sections may depend on context)
 *   - File read cache (files may have changed since last read)
 *   - Compact warning suppression (fresh start after compact)
 *   - Turn metadata (new turn epoch)
 *   - Realtime context (refresh workspace state)
 *
 * Sub-agents share process state with the main thread; only main-thread
 * compacts reset module-level caches that sub-agents would otherwise
 * clobber.
 */

#include "nevercode.h"

typedef struct {
    int total_cleanups;
    int last_cleanup_turn;
    long last_cleanup_time;
    bool skill_content_preserved;
} post_compact_state_t;

static post_compact_state_t g_pc = {0};

void post_compact_init(void) {
    memset(&g_pc, 0, sizeof(g_pc));
    g_pc.skill_content_preserved = true;
}

int post_compact_run(bool is_sub) {
    g_pc.total_cleanups++;
    g_pc.last_cleanup_time = nc_wall_sec();

    int actions = 0;

    if (!is_sub) {
        approval_cache_init();
        actions++;

        prompt_suggest_clear();
        actions++;

        psec_invalidate_all();
        actions++;

        frc_clear();
        actions++;
    }

    compact_warn_clear();
    actions++;

    realtime_ctx_refresh();
    actions++;

    turn_meta_init();
    actions++;

    g_pc.skill_content_preserved = true;
    return actions;
}

int post_compact_total(void) {
    return g_pc.total_cleanups;
}

void post_compact_set_turn(int t) {
    g_pc.last_cleanup_turn = t;
}

int post_compact_last_turn(void) {
    return g_pc.last_cleanup_turn;
}

char *post_compact_describe(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Post-compact cleanups: %d", g_pc.total_cleanups);

    if (g_pc.last_cleanup_time > 0) {
        long ago = nc_wall_sec() - g_pc.last_cleanup_time;
        strbuf_appendf(&sb, " (last %lds ago, turn %d)", ago, g_pc.last_cleanup_turn);
    }
    return strbuf_detach(&sb);
}
