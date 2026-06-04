#include "nevercode.h"

typedef struct {
    pid_t pid;
    char *label;
    pstate_phase_t state;
    int exit_code;
    int signal_num;
    long created_ms;
    long started_ms;
    long exited_ms;
    long output_bytes;
    long timeout_ms;
    bool in_use;
} pstate_entry_t;

static struct {
    pstate_entry_t entries[PROC_STATE_MAX_ENTRIES];
    int count;
    bool initialized;
} g_ps;

void process_state_init(void) {
    memset(&g_ps, 0, sizeof(g_ps));
    g_ps.initialized = true;
}

void process_state_free(void) {
    for (int i = 0; i < g_ps.count; i++)
        free(g_ps.entries[i].label);
    memset(&g_ps, 0, sizeof(g_ps));
}

static int find_entry(pid_t pid) {
    for (int i = 0; i < g_ps.count; i++)
        if (g_ps.entries[i].in_use && g_ps.entries[i].pid == pid)
            return i;
    return -1;
}

int process_state_register(pid_t pid, const char *label, long timeout_ms) {
    if (!g_ps.initialized || g_ps.count >= PROC_STATE_MAX_ENTRIES)
        return -1;
    pstate_entry_t *e = &g_ps.entries[g_ps.count];
    memset(e, 0, sizeof(*e));
    e->pid = pid;
    e->label = label ? nc_xstrdup(label) : NULL;
    e->state = PS_CREATED;
    e->created_ms = nc_mono_ms();
    e->timeout_ms = timeout_ms;
    e->in_use = true;
    g_ps.count++;
    return g_ps.count - 1;
}

static bool valid_transition(pstate_phase_t from, pstate_phase_t to) {
    switch (from) {
    case PS_CREATED:
        return (to == PS_STARTING || to == PS_KILLED);
    case PS_STARTING:
        return (to == PS_RUNNING || to == PS_EXITED || to == PS_KILLED);
    case PS_RUNNING:
        return (to == PS_YIELDED || to == PS_COMPLETING || to == PS_EXITED || to == PS_KILLED ||
                to == PS_TIMED_OUT);
    case PS_YIELDED:
        return (to == PS_RUNNING || to == PS_EXITED || to == PS_KILLED);
    case PS_COMPLETING:
        return (to == PS_EXITED || to == PS_KILLED);
    case PS_EXITED:
    case PS_KILLED:
        return false;
    case PS_TIMED_OUT:
        return (to == PS_KILLED);
    default:
        return false;
    }
}

int process_state_transition(pid_t pid, pstate_phase_t to) {
    int idx = find_entry(pid);
    if (idx < 0)
        return -1;
    pstate_entry_t *e = &g_ps.entries[idx];
    if (!valid_transition(e->state, to))
        return -1;
    e->state = to;
    if (to == PS_STARTING)
        e->started_ms = nc_mono_ms();
    if (to == PS_EXITED || to == PS_KILLED || to == PS_TIMED_OUT)
        e->exited_ms = nc_mono_ms();
    return 0;
}

int process_state_set_running(pid_t pid) {
    int idx = find_entry(pid);
    if (idx < 0)
        return -1;
    if (g_ps.entries[idx].state == PS_CREATED)
        process_state_transition(pid, PS_STARTING);
    return process_state_transition(pid, PS_RUNNING);
}

int process_state_set_exited(pid_t pid, int exit_code) {
    int idx = find_entry(pid);
    if (idx < 0)
        return -1;
    g_ps.entries[idx].exit_code = exit_code;
    return process_state_transition(pid, PS_EXITED);
}

int process_state_set_killed(pid_t pid, int signal_num) {
    int idx = find_entry(pid);
    if (idx < 0)
        return -1;
    g_ps.entries[idx].signal_num = signal_num;
    return process_state_transition(pid, PS_KILLED);
}

int process_state_yield(pid_t pid) {
    return process_state_transition(pid, PS_YIELDED);
}

int process_state_resume(pid_t pid) {
    return process_state_transition(pid, PS_RUNNING);
}

pstate_phase_t process_state_get(pid_t pid) {
    int idx = find_entry(pid);
    return (idx >= 0) ? g_ps.entries[idx].state : PS_EXITED;
}

bool process_state_alive(pid_t pid) {
    int idx = find_entry(pid);
    if (idx < 0)
        return false;
    pstate_phase_t s = g_ps.entries[idx].state;
    return (s == PS_CREATED || s == PS_STARTING || s == PS_RUNNING || s == PS_YIELDED ||
            s == PS_COMPLETING);
}

long process_state_elapsed_ms(pid_t pid) {
    int idx = find_entry(pid);
    if (idx < 0)
        return 0;
    pstate_entry_t *e = &g_ps.entries[idx];
    long start = e->started_ms > 0 ? e->started_ms : e->created_ms;
    long end = e->exited_ms > 0 ? e->exited_ms : nc_mono_ms();
    return end - start;
}

int process_state_total(void) {
    return g_ps.count;
}

int process_state_active_count(void) {
    int c = 0;
    for (int i = 0; i < g_ps.count; i++)
        if (g_ps.entries[i].in_use && process_state_alive(g_ps.entries[i].pid))
            c++;
    return c;
}

const char *pstate_phase_name(pstate_phase_t s) {
    switch (s) {
    case PS_CREATED:
        return "created";
    case PS_STARTING:
        return "starting";
    case PS_RUNNING:
        return "running";
    case PS_YIELDED:
        return "yielded";
    case PS_COMPLETING:
        return "completing";
    case PS_EXITED:
        return "exited";
    case PS_KILLED:
        return "killed";
    case PS_TIMED_OUT:
        return "timed_out";
    default:
        return "?";
    }
}

char *process_state_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "ProcessState: %d total, %d active\n", g_ps.count,
                   process_state_active_count());
    for (int i = 0; i < g_ps.count; i++) {
        pstate_entry_t *e = &g_ps.entries[i];
        if (!e->in_use)
            continue;
        strbuf_appendf(&sb, "  pid=%d label=%s state=%s elapsed=%ldms\n", (int)e->pid,
                       e->label ? e->label : "-", pstate_phase_name(e->state),
                       process_state_elapsed_ms(e->pid));
    }
    return strbuf_detach(&sb);
}
