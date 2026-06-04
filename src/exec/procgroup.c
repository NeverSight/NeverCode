/*
 * procgroup.c — Process group management for tool child processes
 *
 * Groups child processes spawned by tools (bash, agent, repl, etc.) into
 * process groups for coordinated signal delivery and graceful shutdown.
 * Tracks PIDs, enforces per-group timeouts, and handles orphan cleanup.
 */

#include "nevercode.h"

static procgroup_ctx_t g_procgroup;
static bool g_procgroup_initialized = false;

void procgroup_init(procgroup_ctx_t *pg) {
    memset(pg, 0, sizeof(*pg));
}

void procgroup_free(procgroup_ctx_t *pg) {
    for (int i = 0; i < pg->num_entries; i++) {
        free(pg->entries[i].label);
    }
    pg->num_entries = 0;
}

int procgroup_register(procgroup_ctx_t *pg, pid_t pid, const char *label, procgroup_type_t type) {
    if (pg->num_entries >= MAX_PROCGROUP)
        return -1;
    if (pid <= 0)
        return -1;

    for (int i = 0; i < pg->num_entries; i++) {
        if (pg->entries[i].pid == pid)
            return i;
    }

    int idx = pg->num_entries++;
    pg->entries[idx].pid = pid;
    pg->entries[idx].label = label ? nc_xstrdup(label) : NULL;
    pg->entries[idx].type = type;
    pg->entries[idx].start_time = nc_time_now();
    pg->entries[idx].exited = false;
    pg->entries[idx].exit_code = -1;

    return idx;
}

int procgroup_unregister(procgroup_ctx_t *pg, pid_t pid) {
    for (int i = 0; i < pg->num_entries; i++) {
        if (pg->entries[i].pid == pid) {
            free(pg->entries[i].label);
            pg->entries[i] = pg->entries[--pg->num_entries];
            return 0;
        }
    }
    return -1;
}

int procgroup_signal_all(procgroup_ctx_t *pg, int sig) {
    int sent = 0;
    for (int i = 0; i < pg->num_entries; i++) {
        if (!pg->entries[i].exited) {
            if (kill(pg->entries[i].pid, sig) == 0)
                sent++;
        }
    }
    return sent;
}

int procgroup_reap(procgroup_ctx_t *pg) {
    int reaped = 0;
    for (int i = 0; i < pg->num_entries; i++) {
        if (pg->entries[i].exited)
            continue;
        int status;
        pid_t ret = waitpid(pg->entries[i].pid, &status, WNOHANG);
        if (ret > 0) {
            pg->entries[i].exited = true;
            if (WIFEXITED(status))
                pg->entries[i].exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                pg->entries[i].exit_code = 128 + WTERMSIG(status);
            reaped++;
        } else if (ret < 0 && errno == ECHILD) {
            pg->entries[i].exited = true;
            pg->entries[i].exit_code = -1;
            reaped++;
        }
    }
    return reaped;
}

int procgroup_cleanup_exited(procgroup_ctx_t *pg) {
    int removed = 0;
    int i = 0;
    while (i < pg->num_entries) {
        if (pg->entries[i].exited) {
            free(pg->entries[i].label);
            pg->entries[i] = pg->entries[--pg->num_entries];
            removed++;
        } else {
            i++;
        }
    }
    return removed;
}

int procgroup_graceful_shutdown(procgroup_ctx_t *pg, int timeout_s) {
    procgroup_signal_all(pg, SIGTERM);

    for (int elapsed = 0; elapsed < timeout_s; elapsed++) {
        procgroup_reap(pg);
        bool all_done = true;
        for (int i = 0; i < pg->num_entries; i++) {
            if (!pg->entries[i].exited) {
                all_done = false;
                break;
            }
        }
        if (all_done)
            break;
        usleep(PROC_POLL_INTERVAL_US);
    }

    int killed = 0;
    for (int i = 0; i < pg->num_entries; i++) {
        if (!pg->entries[i].exited) {
            kill(pg->entries[i].pid, SIGKILL);
            killed++;
        }
    }

    if (killed > 0) {
        usleep(PROC_POLL_INTERVAL_US);
        procgroup_reap(pg);
    }

    int total = pg->num_entries;
    procgroup_cleanup_exited(pg);
    return total;
}

int procgroup_count(const procgroup_ctx_t *pg) {
    return pg->num_entries;
}

int procgroup_active_count(const procgroup_ctx_t *pg) {
    int count = 0;
    for (int i = 0; i < pg->num_entries; i++) {
        if (!pg->entries[i].exited)
            count++;
    }
    return count;
}

int procgroup_count_by_type(const procgroup_ctx_t *pg, procgroup_type_t type) {
    int count = 0;
    for (int i = 0; i < pg->num_entries; i++) {
        if (pg->entries[i].type == type && !pg->entries[i].exited)
            count++;
    }
    return count;
}

bool procgroup_has_pid(const procgroup_ctx_t *pg, pid_t pid) {
    for (int i = 0; i < pg->num_entries; i++) {
        if (pg->entries[i].pid == pid)
            return true;
    }
    return false;
}

long procgroup_elapsed_s(const procgroup_ctx_t *pg, pid_t pid) {
    for (int i = 0; i < pg->num_entries; i++) {
        if (pg->entries[i].pid == pid)
            return (long)(nc_time_now() - pg->entries[i].start_time);
    }
    return -1;
}

char *procgroup_format(const procgroup_ctx_t *pg) {
    if (pg->num_entries == 0)
        return nc_xstrdup("No tracked processes.\n");

    strbuf_t sb;
    strbuf_init(&sb);

    const char *type_names[] = {"tool", "agent", "repl", "mcp", "other"};
    time_t now = nc_time_now();

    for (int i = 0; i < pg->num_entries; i++) {
        const procgroup_entry_t *e = &pg->entries[i];
        const char *tname =
            (e->type >= 0 && e->type <= PROCGROUP_OTHER) ? type_names[e->type] : "?";
        char line[NC_LABEL_MAX];
        if (e->exited) {
            snprintf(line, sizeof(line), "  PID %d [%s] %s — exited (%d)\n", e->pid, tname,
                     e->label ? e->label : "-", e->exit_code);
        } else {
            long secs = (long)(now - e->start_time);
            snprintf(line, sizeof(line), "  PID %d [%s] %s — running %lds\n", e->pid, tname,
                     e->label ? e->label : "-", secs);
        }
        strbuf_append_str(&sb, line);
    }

    return strbuf_detach(&sb);
}

procgroup_ctx_t *procgroup_global(void) {
    if (!g_procgroup_initialized) {
        procgroup_init(&g_procgroup);
        g_procgroup_initialized = true;
    }
    return &g_procgroup;
}
