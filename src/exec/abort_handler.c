/*
 * abort_handler.c — Graceful task and agent abort with cascading cleanup.
 *
 * Provides structured abort handling that cleanly terminates running
 * tools, agents, and subprocesses while preserving session state for
 * potential resume.
 */

#include "nevercode.h"

/* ABORT_MAX_TASKS, ABORT_GRACE_MS, ABORT_FORCE_MS in defaults.h */

typedef enum {
    ABORT_NONE = 0,
    ABORT_REQUESTED,
    ABORT_GRACEFUL,
    ABORT_FORCED,
    ABORT_COMPLETED
} abort_phase_t;

/* abort_reason_t defined in nevercode.h */

typedef struct {
    char *task_id;
    abort_phase_t phase;
    abort_reason_t reason;
    pid_t pid;
    long requested_ms;
    long completed_ms;
    bool state_saved;
} abort_entry_t;

static struct {
    abort_entry_t entries[ABORT_MAX_TASKS];
    int count;
    int total_aborted;
    int total_forced;
    bool global_abort;
    abort_reason_t global_reason;
} g_abort;

void abort_handler_init(void) {
    memset(&g_abort, 0, sizeof(g_abort));
}

void abort_handler_free(void) {
    for (int i = 0; i < g_abort.count; i++)
        free(g_abort.entries[i].task_id);
    memset(&g_abort, 0, sizeof(g_abort));
}

static int _find_entry(const char *task_id) {
    for (int i = 0; i < g_abort.count; i++)
        if (g_abort.entries[i].task_id && strcmp(g_abort.entries[i].task_id, task_id) == 0)
            return i;
    return -1;
}

int abort_request(const char *task_id, abort_reason_t reason) {
    if (!task_id)
        return -1;

    int idx = _find_entry(task_id);
    if (idx >= 0)
        return 0;

    if (g_abort.count >= ABORT_MAX_TASKS)
        return -1;

    abort_entry_t *e = &g_abort.entries[g_abort.count++];
    e->task_id = nc_xstrdup(task_id);
    e->phase = ABORT_REQUESTED;
    e->reason = reason;
    e->requested_ms = nc_mono_ms();
    e->pid = 0;
    e->state_saved = false;
    return 0;
}

void abort_global(abort_reason_t reason) {
    g_abort.global_abort = true;
    g_abort.global_reason = reason;

    procgroup_ctx_t *pg = procgroup_global();
    if (pg)
        procgroup_graceful_shutdown(pg, PROCGROUP_SHUTDOWN_TIMEOUT);
}

bool abort_is_global(void) {
    return g_abort.global_abort;
}

bool abort_is_pending(const char *task_id) {
    int idx = _find_entry(task_id);
    if (idx < 0)
        return false;
    return g_abort.entries[idx].phase != ABORT_COMPLETED;
}

int abort_pending_count(void) {
    int n = 0;
    for (int i = 0; i < g_abort.count; i++)
        if (g_abort.entries[i].phase != ABORT_COMPLETED)
            n++;
    return n;
}

int abort_total_count(void) {
    return g_abort.total_aborted;
}

int abort_forced_count(void) {
    return g_abort.total_forced;
}

static const char *_reason_name(abort_reason_t r) {
    switch (r) {
    case ABORT_USER_REQ:
        return "user";
    case ABORT_TIMEOUT_REQ:
        return "timeout";
    case ABORT_ERROR_REQ:
        return "error";
    case ABORT_RESOURCE_REQ:
        return "resource";
    case ABORT_SIGNAL_REQ:
        return "signal";
    }
    return "unknown";
}

static const char *_phase_name(abort_phase_t p) {
    switch (p) {
    case ABORT_NONE:
        return "none";
    case ABORT_REQUESTED:
        return "requested";
    case ABORT_GRACEFUL:
        return "graceful";
    case ABORT_FORCED:
        return "forced";
    case ABORT_COMPLETED:
        return "completed";
    }
    return "unknown";
}

char *abort_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Abort: total=%d forced=%d pending=%d global=%s\n", g_abort.total_aborted,
                   g_abort.total_forced, abort_pending_count(),
                   g_abort.global_abort ? "yes" : "no");
    for (int i = 0; i < g_abort.count; i++) {
        abort_entry_t *e = &g_abort.entries[i];
        strbuf_appendf(&sb, "  %s: phase=%s reason=%s", e->task_id, _phase_name(e->phase),
                       _reason_name(e->reason));
        if (e->pid > 0)
            strbuf_appendf(&sb, " pid=%d", (int)e->pid);
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}
