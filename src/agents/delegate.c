/*
 * delegate.c — Sub-agent delegation and lifecycle
 *
 * Manages spawning of interactive and one-shot sub-agents,
 * forwarding events to them, and collecting their results.
 */

#include "nevercode.h"
#include "harness.h"

typedef struct {
    char *id;
    char *parent_id;
    pid_t pid;
    int pipe_in;
    int pipe_out;
    delegate_phase_t phase;
    long start_ms;
    long end_ms;
    bool one_shot;
    char *result;
    int events_forwarded;
    int approvals_handled;
} delegate_entry_t;

static struct {
    delegate_entry_t entries[DELEGATE_MAX_ENTRIES];
    int count;
    bool initialized;
} g_deleg;

void delegate_init(void) {
    memset(&g_deleg, 0, sizeof(g_deleg));
    g_deleg.initialized = true;
}

void delegate_free(void) {
    for (int i = 0; i < g_deleg.count; i++) {
        delegate_entry_t *e = &g_deleg.entries[i];
        free(e->id);
        free(e->parent_id);
        free(e->result);
        if (e->pipe_in > 0)
            close(e->pipe_in);
        if (e->pipe_out > 0)
            close(e->pipe_out);
    }
    memset(&g_deleg, 0, sizeof(g_deleg));
}

static int find_slot(const char *id) {
    for (int i = 0; i < g_deleg.count; i++)
        if (g_deleg.entries[i].id && strcmp(g_deleg.entries[i].id, id) == 0)
            return i;
    return -1;
}

char *delegate_spawn_interactive(const char *parent_id, const char *model, int max_turns,
                                 const char *system_prompt) {
    if (!g_deleg.initialized || g_deleg.count >= DELEGATE_MAX_ENTRIES)
        return NULL;

    delegate_entry_t *e = &g_deleg.entries[g_deleg.count];
    memset(e, 0, sizeof(*e));

    strbuf_t ib;
    strbuf_init(&ib);
    strbuf_appendf(&ib, "deleg_%d_%ld", g_deleg.count, nc_mono_ms() % 100000);
    e->id = strbuf_detach(&ib);
    e->parent_id = parent_id ? nc_xstrdup(parent_id) : NULL;
    e->phase = DELEG_RUNNING;
    e->start_ms = nc_mono_ms();

    (void)model;
    (void)max_turns;
    (void)system_prompt;

    g_deleg.count++;
    return nc_xstrdup(e->id);
}

char *delegate_spawn_one_shot(const char *parent_id, const char *model, const char *prompt) {
    char *id = delegate_spawn_interactive(parent_id, model, 1, NULL);
    if (!id)
        return NULL;

    int idx = find_slot(id);
    if (idx >= 0)
        g_deleg.entries[idx].one_shot = true;
    (void)prompt;
    return id;
}

int delegate_send_input(const char *id, const char *input) {
    int idx = find_slot(id);
    if (idx < 0 || !input)
        return -1;
    delegate_entry_t *e = &g_deleg.entries[idx];
    if (e->phase != DELEG_RUNNING)
        return -1;
    return 0;
}

int delegate_cancel(const char *id) {
    int idx = find_slot(id);
    if (idx < 0)
        return -1;

    delegate_entry_t *e = &g_deleg.entries[idx];
    if (e->phase == DELEG_DONE || e->phase == DELEG_CANCELLED)
        return 0;

    e->phase = DELEG_CANCELLED;
    if (e->pid > 0) {
        kill(e->pid, SIGTERM);
        usleep(DELEGATE_SHUTDOWN_MS * 1000);
    }
    e->phase = DELEG_DONE;
    e->end_ms = nc_mono_ms();
    return 0;
}

int delegate_wait(const char *id, int timeout_ms, char **result_out) {
    int idx = find_slot(id);
    if (idx < 0)
        return -1;

    if (result_out)
        *result_out = NULL;

    delegate_entry_t *e = &g_deleg.entries[idx];
    if (e->phase == DELEG_DONE || e->phase == DELEG_CANCELLED) {
        if (result_out && e->result)
            *result_out = nc_xstrdup(e->result);
        return 0;
    }
    (void)timeout_ms;
    return -1;
}

delegate_phase_t delegate_phase(const char *id) {
    int idx = find_slot(id);
    return (idx >= 0) ? g_deleg.entries[idx].phase : DELEG_IDLE;
}

int delegate_count(void) {
    return g_deleg.count;
}

int delegate_active_count(void) {
    int c = 0;
    for (int i = 0; i < g_deleg.count; i++)
        if (g_deleg.entries[i].phase == DELEG_RUNNING ||
            g_deleg.entries[i].phase == DELEG_COMPLETING)
            c++;
    return c;
}

const char *delegate_phase_name(delegate_phase_t p) {
    switch (p) {
    case DELEG_IDLE:
        return "idle";
    case DELEG_RUNNING:
        return "running";
    case DELEG_COMPLETING:
        return "completing";
    case DELEG_CANCELLED:
        return "cancelled";
    case DELEG_DONE:
        return "done";
    default:
        return "?";
    }
}

char *delegate_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Delegates: %d total, %d active\n", g_deleg.count, delegate_active_count());

    for (int i = 0; i < g_deleg.count; i++) {
        delegate_entry_t *e = &g_deleg.entries[i];
        long elapsed = (e->end_ms > 0 ? e->end_ms : nc_mono_ms()) - e->start_ms;
        strbuf_appendf(&sb, "  [%s] %s parent=%s events=%d %ldms\n", e->id ? e->id : "?",
                       delegate_phase_name(e->phase), e->parent_id ? e->parent_id : "-",
                       e->events_forwarded, elapsed);
    }
    return strbuf_detach(&sb);
}
