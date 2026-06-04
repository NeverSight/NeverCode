/*
 * chunk_policy.c — Adaptive stream chunking for smooth output display.
 *
 * Two-gear system: SMOOTH mode drains one line per tick for baseline
 * pacing; CATCHUP mode flushes all queued lines when backlog builds.
 * Hysteresis thresholds prevent rapid mode flapping.
 */

#include "terminal.h"
#include "nevercode.h"

#define ts_diff_ms nc_ts_diff_ms

void chunk_policy_init(chunk_policy_t *cp) {
    if (!cp)
        return;
    memset(cp, 0, sizeof(*cp));
    cp->mode = CHUNK_SMOOTH;
}

static bool should_enter_catchup(const chunk_policy_t *cp, int queued, int64_t age_ms) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (cp->last_exit.tv_sec > 0) {
        int64_t since_exit = ts_diff_ms(&now, &cp->last_exit);
        if (since_exit < NC_CHUNK_REENTER_HOLD_MS) {
            if (queued < NC_CHUNK_SEVERE_DEPTH && age_ms < NC_CHUNK_SEVERE_AGE_MS)
                return false;
        }
    }

    return queued >= NC_CHUNK_ENTER_DEPTH || age_ms >= NC_CHUNK_ENTER_AGE_MS;
}

static bool should_exit_catchup(const chunk_policy_t *cp, int queued, int64_t age_ms) {
    (void)cp;
    return queued <= NC_CHUNK_EXIT_DEPTH && age_ms <= NC_CHUNK_EXIT_AGE_MS;
}

int chunk_policy_drain_count(chunk_policy_t *cp, int queued, int64_t age_ms) {
    if (!cp || queued <= 0)
        return 0;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    cp->queued_lines = queued;
    cp->oldest_age_ms = age_ms;

    if (cp->mode == CHUNK_SMOOTH) {
        if (should_enter_catchup(cp, queued, age_ms)) {
            cp->mode = CHUNK_CATCHUP;
            cp->last_enter = now;
            return queued;
        }
        return NC_CHUNK_SMOOTH_LINES < queued ? NC_CHUNK_SMOOTH_LINES : queued;
    }

    /* CATCHUP mode */
    if (should_exit_catchup(cp, queued, age_ms)) {
        struct timespec enter = cp->last_enter;
        int64_t in_catchup = ts_diff_ms(&now, &enter);
        if (in_catchup >= NC_CHUNK_EXIT_HOLD_MS) {
            cp->mode = CHUNK_SMOOTH;
            cp->last_exit = now;
            return NC_CHUNK_SMOOTH_LINES < queued ? NC_CHUNK_SMOOTH_LINES : queued;
        }
    }

    return queued;
}
