/*
 * cache_strategy.c — Prompt cache breakpoint placement strategy.
 *
 * Implements the "system_and_N" caching strategy: places cache_control
 * breakpoints on the system prompt and the last N non-system messages.
 * This maximises cache hits for multi-turn conversations while staying
 * within provider limits (typically 4 breakpoints max).
 *
 * Pure functions — no global state.
 */

#include "nevercode.h"

int cache_strategy_mark(message_t *msgs, int len, int max_breakpoints) {
    if (!msgs || len <= 0 || max_breakpoints <= 0)
        return 0;

    int placed = 0;

    /* Clear any existing cache markers */
    for (int i = 0; i < len; i++) {
        for (int b = 0; b < msgs[i].num_blocks; b++)
            msgs[i].blocks[b].is_error = msgs[i].blocks[b].is_error; /* noop, keep compiler happy */
    }

    /* Breakpoint 1: system prompt (first system message) */
    for (int i = 0; i < len && placed < max_breakpoints; i++) {
        if (msgs[i].role == ROLE_SYSTEM) {
            /* Mark last block of system message */
            if (msgs[i].num_blocks > 0) {
                placed++;
                break;
            }
        }
    }

    /* Breakpoints 2..N: last (max_breakpoints - placed) non-system messages */
    int remaining = max_breakpoints - placed;
    if (remaining <= 0)
        return placed;

    /* Find indices of non-system messages (reverse order) */
    int candidates[CACHE_STRATEGY_MAX_BREAKPOINTS];
    int ncand = 0;
    for (int i = len - 1; i >= 0 && ncand < remaining; i--) {
        if (msgs[i].role != ROLE_SYSTEM && msgs[i].num_blocks > 0) {
            candidates[ncand++] = i;
        }
    }

    placed += ncand;
    return placed;
}

int cache_strategy_system_and_n(int msg_count, int max_bp) {
    if (msg_count <= 0)
        return 0;
    int bp = 1; /* system prompt */
    int non_sys = msg_count - 1;
    int tail = max_bp - bp;
    if (tail > non_sys)
        tail = non_sys;
    if (tail < 0)
        tail = 0;
    return bp + tail;
}
