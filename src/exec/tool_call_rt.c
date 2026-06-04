/*
 * tool_call_rt.c — Unified tool call runtime with parallel tracking.
 *
 * Wraps the tool router with execution tracking, diff recording,
 * cancellation support, and parallel call coordination.
 */

#include "nevercode.h"

/* TCRT_MAX_CALLS in defaults.h */

typedef enum { TC_PENDING = 0, TC_RUNNING, TC_COMPLETED, TC_FAILED, TC_CANCELLED } tc_state_t;

typedef struct {
    char *call_id;
    char *tool_name;
    cJSON *arguments;
    char *output;
    tc_state_t state;
    int exit_code;
    long start_ms;
    long elapsed_ms;
    int lines_added;
    int lines_removed;
} tc_entry_t;

static struct {
    tc_entry_t calls[TCRT_MAX_CALLS];
    int count;
    int total_completed;
    int total_failed;
    int total_cancelled;
    long total_exec_ms;
} g_tcrt;

void tool_call_rt_init(void) {
    memset(&g_tcrt, 0, sizeof(g_tcrt));
}

static void _free_entry(tc_entry_t *e) {
    free(e->call_id);
    free(e->tool_name);
    if (e->arguments)
        cJSON_Delete(e->arguments);
    free(e->output);
    memset(e, 0, sizeof(*e));
}

void tool_call_rt_free(void) {
    for (int i = 0; i < g_tcrt.count; i++)
        _free_entry(&g_tcrt.calls[i]);
    memset(&g_tcrt, 0, sizeof(g_tcrt));
}

void tool_call_rt_reset(void) {
    for (int i = 0; i < g_tcrt.count; i++)
        _free_entry(&g_tcrt.calls[i]);
    g_tcrt.count = 0;
}

int tool_call_rt_enqueue(const char *call_id, const char *tool_name, cJSON *arguments) {
    if (g_tcrt.count >= TCRT_MAX_CALLS)
        return -1;

    tc_entry_t *e = &g_tcrt.calls[g_tcrt.count++];
    e->call_id = call_id ? nc_xstrdup(call_id) : NULL;
    e->tool_name = tool_name ? nc_xstrdup(tool_name) : NULL;
    e->arguments = arguments ? cJSON_Duplicate(arguments, 1) : NULL;
    e->state = TC_PENDING;
    return g_tcrt.count - 1;
}

int tool_call_rt_execute(int idx, const tool_def_t *tools, int nt, void *ctx) {
    if (idx < 0 || idx >= g_tcrt.count)
        return -1;
    tc_entry_t *e = &g_tcrt.calls[idx];
    if (e->state != TC_PENDING)
        return -1;

    const tool_def_t *tool = NULL;
    for (int i = 0; i < nt; i++) {
        if (strcmp(tools[i].name, e->tool_name) == 0) {
            tool = &tools[i];
            break;
        }
    }
    if (!tool) {
        e->state = TC_FAILED;
        e->output = nc_xstrdup("tool not found");
        g_tcrt.total_failed++;
        return -1;
    }

    e->state = TC_RUNNING;
    e->start_ms = nc_mono_ms();

    char *output = NULL;
    int rc = tool->execute(e->arguments, &output, ctx);

    e->elapsed_ms = nc_mono_ms() - e->start_ms;
    e->output = output;
    e->exit_code = rc;
    e->state = (rc == 0) ? TC_COMPLETED : TC_FAILED;

    if (rc == 0)
        g_tcrt.total_completed++;
    else
        g_tcrt.total_failed++;

    g_tcrt.total_exec_ms += e->elapsed_ms;

    turn_diff_record(state_turn_id(), e->tool_name, e->lines_added, e->lines_removed);

    return rc;
}

int tool_call_rt_cancel(int idx) {
    if (idx < 0 || idx >= g_tcrt.count)
        return -1;
    tc_entry_t *e = &g_tcrt.calls[idx];
    if (e->state == TC_COMPLETED || e->state == TC_CANCELLED)
        return 0;
    e->state = TC_CANCELLED;
    g_tcrt.total_cancelled++;
    return 0;
}

tc_state_t tool_call_rt_state(int idx) {
    if (idx < 0 || idx >= g_tcrt.count)
        return TC_PENDING;
    return g_tcrt.calls[idx].state;
}

int tool_call_rt_count(void) {
    return g_tcrt.count;
}

int tool_call_rt_pending_count(void) {
    int n = 0;
    for (int i = 0; i < g_tcrt.count; i++)
        if (g_tcrt.calls[i].state == TC_PENDING)
            n++;
    return n;
}

int tool_call_rt_completed_total(void) {
    return g_tcrt.total_completed;
}

static const char *_state_name(tc_state_t s) {
    switch (s) {
    case TC_PENDING:
        return "pending";
    case TC_RUNNING:
        return "running";
    case TC_COMPLETED:
        return "completed";
    case TC_FAILED:
        return "failed";
    case TC_CANCELLED:
        return "cancelled";
    }
    return "unknown";
}

char *tool_call_rt_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "ToolCallRT: total=%d ok=%d fail=%d cancel=%d exec=%ldms\n", g_tcrt.count,
                   g_tcrt.total_completed, g_tcrt.total_failed, g_tcrt.total_cancelled,
                   g_tcrt.total_exec_ms);
    for (int i = 0; i < g_tcrt.count; i++) {
        tc_entry_t *e = &g_tcrt.calls[i];
        strbuf_appendf(&sb, "  [%d] %s: %s %ldms rc=%d\n", i, e->tool_name ? e->tool_name : "?",
                       _state_name(e->state), e->elapsed_ms, e->exit_code);
    }
    return strbuf_detach(&sb);
}
