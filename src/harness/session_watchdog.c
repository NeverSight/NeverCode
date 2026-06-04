/*
 * session_watchdog.c — Per-turn timeout guard and auto-recovery
 *
 * Monitors individual agent turns for wall-clock timeout.  When a turn
 * exceeds the configured deadline, the watchdog fires, allowing the
 * caller to abort the current API call and inject a recovery message.
 *
 * The watchdog also tracks consecutive timeouts — if too many pile up,
 * it recommends a context handoff (fresh session).
 */

#include "harness.h"

static watchdog_ctx_t g_wd;
/* 0=none, 1=timeout, 2=handoff — set when xfer poll aborts HTTP stream */
static volatile sig_atomic_t g_xfer_wd_verdict;

static int watchdog_normalize_deadline(int deadline_ms) {
    int ms = deadline_ms;
    if (ms <= 0)
        ms = WATCHDOG_DEFAULT_DEADLINE_MS;
    if (ms > WATCHDOG_MAX_DEADLINE_MS)
        ms = WATCHDOG_MAX_DEADLINE_MS;
    return ms;
}

void watchdog_init(int deadline_ms) {
    memset(&g_wd, 0, sizeof(g_wd));
    g_wd.deadline_ms = watchdog_normalize_deadline(deadline_ms);
    g_wd.state = WD_IDLE;
    g_xfer_wd_verdict = 0;
}

void watchdog_free(void) {
    memset(&g_wd, 0, sizeof(g_wd));
    g_xfer_wd_verdict = 0;
}

void watchdog_begin_turn(int turn) {
    g_wd.current_turn = turn;
    g_wd.turn_start_ms = nc_mono_ms();
    g_wd.state = WD_RUNNING;
    g_wd.total_turns++;
}

void watchdog_end_turn(bool success) {
    if (g_wd.state != WD_RUNNING && g_wd.state != WD_FIRED)
        return;

    long elapsed = nc_mono_ms() - g_wd.turn_start_ms;

    if (elapsed > g_wd.longest_turn_ms)
        g_wd.longest_turn_ms = elapsed;

    if (success) {
        g_wd.consecutive_timeouts = 0;
        g_wd.successful_turns++;
    }

    g_wd.state = WD_IDLE;
}

long watchdog_elapsed_ms(void) {
    if (g_wd.state != WD_RUNNING && g_wd.state != WD_FIRED)
        return 0;
    return nc_mono_ms() - g_wd.turn_start_ms;
}

watchdog_verdict_t watchdog_check(void) {
    if (g_wd.state != WD_RUNNING)
        return WD_VERDICT_OK;

    long elapsed = watchdog_elapsed_ms();

    if (elapsed < (g_wd.deadline_ms * WATCHDOG_WARN_PCT / 100))
        return WD_VERDICT_OK;

    if (elapsed < g_wd.deadline_ms)
        return WD_VERDICT_WARN;

    g_wd.state = WD_FIRED;
    g_wd.timeout_count++;
    g_wd.consecutive_timeouts++;

    if (g_wd.consecutive_timeouts >= WATCHDOG_HANDOFF_THRESHOLD)
        return WD_VERDICT_HANDOFF;

    return WD_VERDICT_TIMEOUT;
}

void watchdog_set_deadline(int ms) {
    g_wd.deadline_ms = watchdog_normalize_deadline(ms);
}

int watchdog_stream_xfer_poll(void) {
    watchdog_verdict_t v = watchdog_check();
    if (v == WD_VERDICT_TIMEOUT) {
        g_xfer_wd_verdict = 1;
        return 1;
    }
    if (v == WD_VERDICT_HANDOFF) {
        g_xfer_wd_verdict = 2;
        return 1;
    }
    return 0;
}

watchdog_verdict_t watchdog_stream_xfer_take_verdict(void) {
    int x = (int)g_xfer_wd_verdict;
    g_xfer_wd_verdict = 0;
    if (x == 1)
        return WD_VERDICT_TIMEOUT;
    if (x == 2)
        return WD_VERDICT_HANDOFF;
    return WD_VERDICT_OK;
}

char *watchdog_recovery_prompt(watchdog_verdict_t v) {
    switch (v) {
    case WD_VERDICT_OK:
    case WD_VERDICT_WARN:
        return nc_xstrdup("");
    default:
        break;
    }

    strbuf_t sb;
    strbuf_init(&sb);

    switch (v) {
    case WD_VERDICT_TIMEOUT:
        strbuf_append_str(
            &sb, "The previous turn timed out after exceeding the wall-clock deadline. "
                 "This typically happens when an API call or tool execution hangs. "
                 "Please retry the operation with a simpler approach, or skip it and move on.");
        break;
    case WD_VERDICT_HANDOFF: {
        char buf[NC_MSG_MAX];
        snprintf(buf, sizeof(buf),
                 "Multiple consecutive turns (%d) have timed out. "
                 "The session context may be degraded. Consider starting a new "
                 "conversation with a handoff summary, or simplify the current task.",
                 g_wd.consecutive_timeouts);
        strbuf_append_str(&sb, buf);
        break;
    }
    default:
        return nc_xstrdup("");
    }

    return strbuf_detach(&sb);
}

char *watchdog_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    const char *state_name = "idle";
    switch (g_wd.state) {
    case WD_RUNNING:
        state_name = "running";
        break;
    case WD_FIRED:
        state_name = "fired";
        break;
    default:
        break;
    }

    char line[NC_MSG_MAX];
    snprintf(line, sizeof(line), "  state        %s\n", state_name);
    strbuf_append_str(&sb, line);
    snprintf(line, sizeof(line), "  deadline     %d ms\n", g_wd.deadline_ms);
    strbuf_append_str(&sb, line);
    snprintf(line, sizeof(line), "  turns        %d total, %d ok, %d timeout\n", g_wd.total_turns,
             g_wd.successful_turns, g_wd.timeout_count);
    strbuf_append_str(&sb, line);
    snprintf(line, sizeof(line), "  longest      %ld ms\n", g_wd.longest_turn_ms);
    strbuf_append_str(&sb, line);

    if (g_wd.consecutive_timeouts > 0) {
        snprintf(line, sizeof(line), "  consecutive  %d timeouts\n", g_wd.consecutive_timeouts);
        strbuf_append_str(&sb, line);
    }

    if (g_wd.state == WD_RUNNING) {
        long el = watchdog_elapsed_ms();
        int pct = (int)(el * 100 / g_wd.deadline_ms);
        snprintf(line, sizeof(line), "  elapsed      %ld ms (%d%%)\n", el, pct);
        strbuf_append_str(&sb, line);
    }

    return strbuf_detach(&sb);
}

int watchdog_timeout_count(void) {
    return g_wd.timeout_count;
}

int watchdog_consecutive_timeouts(void) {
    return g_wd.consecutive_timeouts;
}

long watchdog_longest_turn_ms(void) {
    return g_wd.longest_turn_ms;
}

void watchdog_snapshot(watchdog_snapshot_t *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->deadline_ms = g_wd.deadline_ms;
    out->total_turns = g_wd.total_turns;
    out->successful_turns = g_wd.successful_turns;
    out->timeout_count = g_wd.timeout_count;
    out->consecutive_timeouts = g_wd.consecutive_timeouts;
    out->longest_turn_ms = g_wd.longest_turn_ms;
}

void watchdog_restore(const watchdog_snapshot_t *snap) {
    watchdog_free();
    if (!snap) {
        watchdog_init(0);
        return;
    }
    g_wd.deadline_ms = watchdog_normalize_deadline(snap->deadline_ms);
    g_wd.total_turns = snap->total_turns;
    g_wd.successful_turns = snap->successful_turns;
    g_wd.timeout_count = snap->timeout_count;
    g_wd.consecutive_timeouts = snap->consecutive_timeouts;
    g_wd.longest_turn_ms = snap->longest_turn_ms;
    g_wd.state = WD_IDLE;
    g_xfer_wd_verdict = 0;
}
