/*
 * frame_limiter.c — Prevents excessive terminal redraws during streaming.
 *
 * Caps frame output rate to NC_FPS_TARGET to avoid wasted work and
 * terminal buffer thrashing. Callers check should_draw() before emitting
 * partial updates.
 */

#include "terminal.h"
#include "nevercode.h"

#define ts_diff_us nc_ts_diff_us

void frame_limiter_init(frame_limiter_t *fl) {
    if (!fl)
        return;
    memset(fl, 0, sizeof(*fl));
}

bool frame_limiter_should_draw(frame_limiter_t *fl) {
    if (!fl)
        return true;
    if (fl->last_draw.tv_sec == 0 && fl->last_draw.tv_nsec == 0)
        return true;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    int64_t elapsed_us = ts_diff_us(&now, &fl->last_draw);
    if (elapsed_us >= NC_FRAME_MIN_INTERVAL_US)
        return true;

    fl->suppressed++;
    return false;
}

void frame_limiter_mark(frame_limiter_t *fl) {
    if (!fl)
        return;
    clock_gettime(CLOCK_MONOTONIC, &fl->last_draw);
    fl->suppressed = 0;
}
