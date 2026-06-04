/*
 * session_status.c — Session state machine (idle/busy/retry/error).
 *
 * Tracks the high-level status of the active session and broadcasts
 * transitions through the event bus.  Used by the UI layer to show
 * appropriate indicators and by the harness to detect stuck states.
 */

#include "nevercode.h"

typedef enum { SS_IDLE = 0, SS_BUSY, SS_RETRY, SS_ERROR, SS_COMPACT, SS_HANDOFF } ss_state_t;

static struct {
    ss_state_t status;
    int retry_attempt;
    char retry_reason[NC_MSG_MAX];
    long retry_next_ms;
    long last_transition;
    int transition_count;
} g_ss;

void session_status_init(void) {
    memset(&g_ss, 0, sizeof(g_ss));
    g_ss.status = SS_IDLE;
    g_ss.last_transition = nc_wall_sec();
}

static const char *status_name(ss_state_t s) {
    switch (s) {
    case SS_IDLE:
        return "idle";
    case SS_BUSY:
        return "busy";
    case SS_RETRY:
        return "retry";
    case SS_ERROR:
        return "error";
    case SS_COMPACT:
        return "compact";
    case SS_HANDOFF:
        return "handoff";
    }
    return "unknown";
}

static void transition(ss_state_t to) {
    g_ss.status = to;
    g_ss.last_transition = nc_wall_sec();
    g_ss.transition_count++;
}

void session_status_set_busy(void) {
    transition(SS_BUSY);
    g_ss.retry_attempt = 0;
    g_ss.retry_reason[0] = '\0';
}

void session_status_set_idle(void) {
    transition(SS_IDLE);
}

void session_status_set_retry(int attempt, const char *reason, long next_ms) {
    transition(SS_RETRY);
    g_ss.retry_attempt = attempt;
    g_ss.retry_next_ms = next_ms;
    if (reason)
        nc_strlcpy(g_ss.retry_reason, reason, sizeof(g_ss.retry_reason));
}

void session_status_set_error(const char *reason) {
    transition(SS_ERROR);
    if (reason)
        nc_strlcpy(g_ss.retry_reason, reason, sizeof(g_ss.retry_reason));
}

void session_status_set_compact(void) {
    transition(SS_COMPACT);
}

void session_status_set_handoff(void) {
    transition(SS_HANDOFF);
}

bool session_status_is_busy(void) {
    return g_ss.status == SS_BUSY || g_ss.status == SS_RETRY || g_ss.status == SS_COMPACT ||
           g_ss.status == SS_HANDOFF;
}

bool session_status_is_idle(void) {
    return g_ss.status == SS_IDLE;
}

const char *session_status_name(void) {
    return status_name(g_ss.status);
}

int session_status_retry_attempt(void) {
    return g_ss.retry_attempt;
}

long session_status_idle_seconds(void) {
    if (g_ss.status != SS_IDLE)
        return 0;
    return nc_wall_sec() - g_ss.last_transition;
}

char *session_status_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    long elapsed = nc_wall_sec() - g_ss.last_transition;
    strbuf_appendf(&sb, "Session: %s (%lds, %d transitions)", status_name(g_ss.status), elapsed,
                   g_ss.transition_count);

    if (g_ss.status == SS_RETRY) {
        strbuf_appendf(&sb, "\n  Retry: attempt %d, reason: %s", g_ss.retry_attempt,
                       g_ss.retry_reason[0] ? g_ss.retry_reason : "unknown");
    } else if (g_ss.status == SS_ERROR && g_ss.retry_reason[0]) {
        strbuf_appendf(&sb, "\n  Error: %s", g_ss.retry_reason);
    }

    return strbuf_detach(&sb);
}
