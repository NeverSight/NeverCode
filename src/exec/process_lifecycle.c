/*
 * process_lifecycle.c — Unified process state machine
 *
 * Tracks spawned processes: SPAWNED -> RUNNING -> EXITED/KILLED
 * Thread-safe state queries and cleanup coordination.
 */

#include "nevercode.h"

typedef enum {
    PLC_INIT = 0,
    PLC_SPAWNED = 1,
    PLC_RUNNING = 2,
    PLC_YIELDED = 3,
    PLC_EXITED = 4,
    PLC_KILLED = 5
} plc_state_t;

typedef struct {
    pid_t pid;
    plc_state_t state;
    char label[NC_TAG_MAX];
    int exit_code;
    time_t spawn_time;
    time_t exit_time;
    long output_bytes;
    bool timed_out;
} proc_lifecycle_t;

static struct {
    proc_lifecycle_t entries[PROC_LIFECYCLE_MAX];
    int count;
    pthread_mutex_t lock;
    bool initialized;
} g_lc;

void proc_lifecycle_init(void) {
    if (g_lc.initialized)
        return;
    memset(&g_lc, 0, sizeof(g_lc));
    pthread_mutex_init(&g_lc.lock, NULL);
    g_lc.initialized = true;
}

void proc_lifecycle_free(void) {
    if (!g_lc.initialized)
        return;
    pthread_mutex_destroy(&g_lc.lock);
    memset(&g_lc, 0, sizeof(g_lc));
}

int proc_lifecycle_register(pid_t pid, const char *label) {
    if (!g_lc.initialized)
        return -1;
    pthread_mutex_lock(&g_lc.lock);
    if (g_lc.count >= PROC_LIFECYCLE_MAX) {
        int ev = -1;
        for (int i = 0; i < g_lc.count; i++)
            if (g_lc.entries[i].state >= PLC_EXITED) {
                ev = i;
                break;
            }
        if (ev >= 0) {
            memmove(&g_lc.entries[ev], &g_lc.entries[ev + 1],
                    (g_lc.count - ev - 1) * sizeof(proc_lifecycle_t));
            g_lc.count--;
        } else {
            pthread_mutex_unlock(&g_lc.lock);
            return -1;
        }
    }
    int idx = g_lc.count++;
    proc_lifecycle_t *p = &g_lc.entries[idx];
    memset(p, 0, sizeof(*p));
    p->pid = pid;
    p->state = PLC_SPAWNED;
    p->spawn_time = nc_time_now();
    if (label)
        nc_strlcpy(p->label, label, sizeof(p->label));
    pthread_mutex_unlock(&g_lc.lock);
    return idx;
}

static proc_lifecycle_t *find_pid(pid_t pid) {
    for (int i = 0; i < g_lc.count; i++)
        if (g_lc.entries[i].pid == pid)
            return &g_lc.entries[i];
    return NULL;
}

void proc_lifecycle_set_running(pid_t pid) {
    if (!g_lc.initialized)
        return;
    pthread_mutex_lock(&g_lc.lock);
    proc_lifecycle_t *p = find_pid(pid);
    if (p && p->state == PLC_SPAWNED)
        p->state = PLC_RUNNING;
    pthread_mutex_unlock(&g_lc.lock);
}

void proc_lifecycle_set_exited(pid_t pid, int exit_code) {
    if (!g_lc.initialized)
        return;
    pthread_mutex_lock(&g_lc.lock);
    proc_lifecycle_t *p = find_pid(pid);
    if (p && p->state < PLC_EXITED) {
        p->state = PLC_EXITED;
        p->exit_code = exit_code;
        p->exit_time = nc_time_now();
    }
    pthread_mutex_unlock(&g_lc.lock);
}

void proc_lifecycle_set_killed(pid_t pid) {
    if (!g_lc.initialized)
        return;
    pthread_mutex_lock(&g_lc.lock);
    proc_lifecycle_t *p = find_pid(pid);
    if (p && p->state < PLC_EXITED) {
        p->state = PLC_KILLED;
        p->exit_time = nc_time_now();
    }
    pthread_mutex_unlock(&g_lc.lock);
}

void proc_lifecycle_record_output(pid_t pid, long bytes) {
    if (!g_lc.initialized)
        return;
    pthread_mutex_lock(&g_lc.lock);
    proc_lifecycle_t *p = find_pid(pid);
    if (p)
        p->output_bytes += bytes;
    pthread_mutex_unlock(&g_lc.lock);
}

int proc_lifecycle_reap_exited(void) {
    if (!g_lc.initialized)
        return 0;
    pthread_mutex_lock(&g_lc.lock);
    int reaped = 0;
    for (int i = 0; i < g_lc.count; i++) {
        proc_lifecycle_t *p = &g_lc.entries[i];
        if (p->state < PLC_EXITED && p->pid > 0) {
            int st;
            pid_t r = waitpid(p->pid, &st, WNOHANG);
            if (r > 0) {
                p->state = PLC_EXITED;
                p->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
                p->exit_time = nc_time_now();
                reaped++;
            }
        }
    }
    pthread_mutex_unlock(&g_lc.lock);
    return reaped;
}

int proc_lifecycle_kill_overtime(int max_seconds) {
    if (!g_lc.initialized)
        return 0;
    pthread_mutex_lock(&g_lc.lock);
    time_t now = nc_time_now();
    int killed = 0;
    for (int i = 0; i < g_lc.count; i++) {
        proc_lifecycle_t *p = &g_lc.entries[i];
        if (p->state < PLC_EXITED && p->pid > 0 && (now - p->spawn_time) > max_seconds) {
            kill(p->pid, SIGTERM);
            usleep(PROC_TERM_GRACE_US);
            kill(p->pid, SIGKILL);
            p->state = PLC_KILLED;
            p->exit_time = now;
            p->timed_out = true;
            killed++;
        }
    }
    pthread_mutex_unlock(&g_lc.lock);
    return killed;
}

int proc_lifecycle_active_count(void) {
    if (!g_lc.initialized)
        return 0;
    pthread_mutex_lock(&g_lc.lock);
    int a = 0;
    for (int i = 0; i < g_lc.count; i++)
        if (g_lc.entries[i].state < PLC_EXITED)
            a++;
    pthread_mutex_unlock(&g_lc.lock);
    return a;
}

int proc_lifecycle_total_count(void) {
    return g_lc.initialized ? g_lc.count : 0;
}

static const char *sname(plc_state_t s) {
    switch (s) {
    case PLC_INIT:
        return "init";
    case PLC_SPAWNED:
        return "spawned";
    case PLC_RUNNING:
        return "running";
    case PLC_YIELDED:
        return "yielded";
    case PLC_EXITED:
        return "exited";
    case PLC_KILLED:
        return "killed";
    }
    return "unknown";
}

char *proc_lifecycle_format(void) {
    if (!g_lc.initialized)
        return nc_xstrdup("(not initialized)");
    pthread_mutex_lock(&g_lc.lock);
    strbuf_t sb;
    strbuf_init(&sb);
    int active = 0;
    for (int i = 0; i < g_lc.count; i++)
        if (g_lc.entries[i].state < PLC_EXITED)
            active++;
    strbuf_appendf(&sb, "Processes: %d total, %d active\n", g_lc.count, active);
    time_t now = nc_time_now();
    for (int i = 0; i < g_lc.count; i++) {
        proc_lifecycle_t *p = &g_lc.entries[i];
        long el = (p->state >= PLC_EXITED) ? (p->exit_time - p->spawn_time) : (now - p->spawn_time);
        strbuf_appendf(&sb, "  pid=%d state=%s label=%s elapsed=%lds out=%ldB", p->pid,
                       sname(p->state), p->label, el, p->output_bytes);
        if (p->state == PLC_EXITED)
            strbuf_appendf(&sb, " exit=%d", p->exit_code);
        if (p->timed_out)
            strbuf_append_str(&sb, " (timeout)");
        strbuf_append_str(&sb, "\n");
    }
    pthread_mutex_unlock(&g_lc.lock);
    return strbuf_detach(&sb);
}
