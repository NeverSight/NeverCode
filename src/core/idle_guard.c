/*
 * idle_guard.c — Idle timeout management for daemon/server mode.
 *
 * Tracks user activity and triggers graceful shutdown when the session
 * has been idle beyond the configured threshold.  Disabled in
 * interactive REPL mode; primarily for SDK/daemon use.
 */

#include "nevercode.h"

static struct {
    bool enabled;
    long timeout_ms;
    long last_activity_ms;
    bool running;
} g_idle;

void idle_guard_init(int timeout_ms) {
    memset(&g_idle, 0, sizeof(g_idle));
    if (timeout_ms <= 0)
        return;
    g_idle.enabled = true;
    g_idle.timeout_ms = (long)timeout_ms;
    g_idle.last_activity_ms = nc_mono_ms();
    g_idle.running = false;
}

void idle_guard_touch(void) {
    if (!g_idle.enabled)
        return;
    g_idle.last_activity_ms = nc_mono_ms();
}

void idle_guard_start(void) {
    if (!g_idle.enabled)
        return;
    g_idle.running = true;
    g_idle.last_activity_ms = nc_mono_ms();
}

void idle_guard_stop(void) {
    g_idle.running = false;
}

bool idle_guard_is_expired(void) {
    if (!g_idle.enabled || !g_idle.running)
        return false;
    long elapsed_ms = nc_mono_ms() - g_idle.last_activity_ms;
    return elapsed_ms >= g_idle.timeout_ms;
}

int idle_guard_remaining_ms(void) {
    if (!g_idle.enabled || !g_idle.running)
        return -1;
    long elapsed_ms = nc_mono_ms() - g_idle.last_activity_ms;
    long remaining = g_idle.timeout_ms - elapsed_ms;
    return remaining > 0 ? (int)remaining : 0;
}

bool idle_guard_enabled(void) {
    return g_idle.enabled;
}

int idle_guard_timeout_ms(void) {
    return g_idle.enabled ? g_idle.timeout_ms : 0;
}
