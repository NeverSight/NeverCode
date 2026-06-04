/*
 * interrupted_turn.c — Interrupted turn handling
 *
 * Manages history markers and recovery when the user
 * interrupts an in-progress turn (Ctrl+C / abort).
 * Ensures partial results are properly recorded.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Constants ── */

#define ABORT_GUIDANCE                                                                             \
    "The user interrupted the previous turn. "                                                     \
    "Any running tools/commands may have partially executed. "                                     \
    "Background processes may still be running."

/* ── Internal types ── */

typedef struct {
    int turn;
    interrupt_reason_t reason;
    long timestamp;
    int tool_calls_pending;
    bool had_partial_output;
} interrupt_record_t;

typedef struct {
    interrupt_record_t history[INTR_HISTORY_MAX];
    int count;
    int total_interrupts;
} interrupt_ctx_t;

static interrupt_ctx_t g_intr;

/* Forward declaration */
const char *interrupt_reason_name(interrupt_reason_t r);

/* ── Init / Free ── */

void interrupt_ctx_init(void) {
    memset(&g_intr, 0, sizeof(g_intr));
}

void interrupt_ctx_free(void) {
    g_intr.count = 0;
    g_intr.total_interrupts = 0;
}

/* ── Recording ── */

int interrupt_record(int turn, interrupt_reason_t reason, int pending_tools, bool partial_output) {
    if (g_intr.count >= INTR_HISTORY_MAX) {
        for (int i = 0; i < INTR_HISTORY_MAX - 1; i++)
            g_intr.history[i] = g_intr.history[i + 1];
        g_intr.count = INTR_HISTORY_MAX - 1;
    }

    interrupt_record_t *r = &g_intr.history[g_intr.count++];
    r->turn = turn;
    r->reason = reason;
    r->timestamp = nc_wall_sec();
    r->tool_calls_pending = pending_tools;
    r->had_partial_output = partial_output;
    g_intr.total_interrupts++;
    return 0;
}

/* ── History marker generation ── */

char *interrupt_build_marker(int turn, interrupt_reason_t reason) {
    size_t sz = WARMUP_FMT_BUF;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    snprintf(buf, sz, "%s\n%s\nTurn: %d | Reason: %s\n%s", INTR_ABORT_OPEN, ABORT_GUIDANCE, turn,
             interrupt_reason_name(reason), INTR_ABORT_CLOSE);
    return buf;
}

/* ── Queries ── */

int interrupt_total_count(void) {
    return g_intr.total_interrupts;
}

bool interrupt_was_recent(int turns_ago) {
    if (g_intr.count == 0)
        return false;
    int last_turn = g_intr.history[g_intr.count - 1].turn;
    return (last_turn >= turns_ago);
}

const char *interrupt_reason_name(interrupt_reason_t r) {
    switch (r) {
    case INTR_USER_CANCEL:
        return "user_cancel";
    case INTR_TIMEOUT:
        return "timeout";
    case INTR_SYSTEM:
        return "system";
    case INTR_COMPACT:
        return "compact";
    }
    return "unknown";
}

char *interrupt_format_history(void) {
    if (g_intr.count == 0)
        return nc_xstrdup("No interrupts recorded.\n");

    size_t sz = (size_t)g_intr.count * 128 + 128;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    int off = snprintf(buf, sz, "Interrupt history (%d total, %d recent):\n",
                       g_intr.total_interrupts, g_intr.count);
    for (int i = 0; i < g_intr.count; i++) {
        const interrupt_record_t *r = &g_intr.history[i];
        off += snprintf(buf + off, sz - (size_t)off, "  turn %d: %s (pending=%d, partial=%s)\n",
                        r->turn, interrupt_reason_name(r->reason), r->tool_calls_pending,
                        r->had_partial_output ? "yes" : "no");
    }
    return buf;
}
