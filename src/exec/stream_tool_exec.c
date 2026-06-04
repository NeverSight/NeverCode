/*
 * stream_tool_exec.c — Streaming tool executor with concurrency control.
 *
 * Manages parallel and exclusive tool execution during streaming.
 * Concurrent-safe tools (read-only) run in parallel; exclusive tools
 * (write operations, bash) serialize access. Results are buffered and
 * emitted in the original tool call order.
 */
#include "nevercode.h"
#include "defaults.h"

typedef enum {
    STE_QUEUED,
    STE_EXECUTING,
    STE_COMPLETED,
    STE_DISCARDED,
} ste_status_t;

typedef struct {
    char call_id[NC_FMT_MAX];
    char tool_name[TOOL_POOL_NAME_MAX];
    ste_status_t status;
    bool concurrent_safe;
    char *result;
    int result_code;
    long start_ms;
    long end_ms;
} ste_slot_t;

typedef struct {
    ste_slot_t slots[STE_MAX_SLOTS];
    int count;
    int active_concurrent;
    bool exclusive_running;
    bool discarded;
    bool has_error;
    char error_tool[TOOL_POOL_NAME_MAX];

} ste_ctx_t;

static ste_ctx_t g_ste;
static bool g_ste_initialized;

static const char *concurrent_safe_tools[] = {"read",        "glob",        "grep",       "listdir",
                                              "tool_search", "file_search", "image_view", "todo",
                                              "plan_mode",   NULL};

static bool is_concurrent_safe(const char *name) {
    if (!name)
        return false;
    for (int i = 0; concurrent_safe_tools[i]; i++)
        if (strcmp(name, concurrent_safe_tools[i]) == 0)
            return true;
    return false;
}

void ste_init(void) {
    memset(&g_ste, 0, sizeof(g_ste));

    g_ste_initialized = true;
}

void ste_free(void) {
    if (!g_ste_initialized)
        return;
    for (int i = 0; i < g_ste.count; i++)
        free(g_ste.slots[i].result);

    memset(&g_ste, 0, sizeof(g_ste));
    g_ste_initialized = false;
}

void ste_discard(void) {

    g_ste.discarded = true;
    for (int i = 0; i < g_ste.count; i++)
        if (g_ste.slots[i].status == STE_QUEUED)
            g_ste.slots[i].status = STE_DISCARDED;
}

int ste_add_tool(const char *call_id, const char *tool_name) {
    if (!call_id || !tool_name)
        return -1;

    if (g_ste.count >= STE_MAX_SLOTS || g_ste.discarded) {

        return -1;
    }
    ste_slot_t *s = &g_ste.slots[g_ste.count];
    memset(s, 0, sizeof(*s));
    nc_strlcpy(s->call_id, call_id, sizeof(s->call_id));
    nc_strlcpy(s->tool_name, tool_name, sizeof(s->tool_name));
    s->status = STE_QUEUED;
    s->concurrent_safe = is_concurrent_safe(tool_name);
    g_ste.count++;

    return g_ste.count - 1;
}

bool ste_can_start(int idx) {
    if (idx < 0 || idx >= g_ste.count)
        return false;

    ste_slot_t *s = &g_ste.slots[idx];
    bool ok = false;
    if (s->status == STE_QUEUED && !g_ste.discarded) {
        if (s->concurrent_safe)
            ok = !g_ste.exclusive_running;
        else
            ok = !g_ste.exclusive_running && g_ste.active_concurrent == 0;
    }

    return ok;
}

void ste_mark_started(int idx) {
    if (idx < 0 || idx >= g_ste.count)
        return;

    ste_slot_t *s = &g_ste.slots[idx];
    s->status = STE_EXECUTING;
    s->start_ms = nc_mono_ms();
    if (s->concurrent_safe)
        g_ste.active_concurrent++;
    else
        g_ste.exclusive_running = true;
}

void ste_mark_completed(int idx, const char *result, int code) {
    if (idx < 0 || idx >= g_ste.count)
        return;

    ste_slot_t *s = &g_ste.slots[idx];
    s->status = STE_COMPLETED;
    s->end_ms = nc_mono_ms();
    free(s->result);
    s->result = result ? nc_xstrdup(result) : NULL;
    s->result_code = code;
    if (s->concurrent_safe) {
        if (g_ste.active_concurrent > 0)
            g_ste.active_concurrent--;
    } else {
        g_ste.exclusive_running = false;
    }
    if (code != 0 && !g_ste.has_error) {
        g_ste.has_error = true;
        nc_strlcpy(g_ste.error_tool, s->tool_name, sizeof(g_ste.error_tool));
    }
}

int ste_pending_count(void) {
    if (!g_ste_initialized)
        return 0;
    int n = 0;

    for (int i = 0; i < g_ste.count; i++)
        if (g_ste.slots[i].status == STE_QUEUED)
            n++;

    return n;
}

int ste_completed_count(void) {
    int n = 0;

    for (int i = 0; i < g_ste.count; i++)
        if (g_ste.slots[i].status == STE_COMPLETED)
            n++;

    return n;
}

int ste_total_count(void) {
    return g_ste.count;
}
bool ste_has_error(void) {
    return g_ste.has_error;
}

char *ste_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_appendf(&sb, "StreamExec: %d tools (%d pending, %d active_concurrent, exclusive=%s)\n",
                   g_ste.count, ste_pending_count(), g_ste.active_concurrent,
                   g_ste.exclusive_running ? "yes" : "no");
    for (int i = 0; i < g_ste.count; i++) {
        ste_slot_t *s = &g_ste.slots[i];
        const char *sn[] = {"queued", "executing", "completed", "discarded"};
        int si = (int)s->status;
        if (si < 0 || si > 3)
            si = 0;
        strbuf_appendf(&sb, "  [%d] %s: %s (%s)%s\n", i, s->tool_name, sn[si],
                       s->concurrent_safe ? "concurrent" : "exclusive",
                       s->result_code != 0 ? " FAILED" : "");
    }

    return strbuf_detach(&sb);
}
