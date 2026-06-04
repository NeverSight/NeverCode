/*
 * process_mgr.c — Unified process store and lifecycle manager.
 *
 * Tracks all spawned child processes with slot-based management,
 * yield-time clamping for interactive terminals, stdin forwarding,
 * and automatic cleanup of expired processes.
 */
#include "nevercode.h"
#include "defaults.h"
#include <poll.h>

/* PM_MAX_PROCESSES .. PM_NAME_MAX in defaults.h */
#define PM_MAX_PROCS PM_MAX_PROCESSES
#define PM_WARN_PROCS PM_WARN_THRESHOLD

typedef enum { PM_FREE = 0, PM_RUNNING, PM_STOPPED, PM_EXITED } pm_state_t;

typedef struct {
    pm_state_t state;
    pid_t pid;
    int slot_id;
    char command[PM_NAME_MAX];
    char workdir[NC_PATH_MAX];
    int exit_code;
    time_t started;
    time_t last_used;
    bool tty;
    int stdin_fd;
    int stdout_fd;
} pm_entry_t;

static struct {
    pm_entry_t procs[PM_MAX_PROCS];
    int active;
    int next_id;
    bool init;
} g_pm;

void pm_init(void) {
    memset(&g_pm, 0, sizeof(g_pm));
    g_pm.next_id = 1;
    g_pm.init = true;
}

static pm_entry_t *pm_find(int id) {
    for (int i = 0; i < PM_MAX_PROCS; i++)
        if (g_pm.procs[i].state != PM_FREE && g_pm.procs[i].slot_id == id)
            return &g_pm.procs[i];
    return NULL;
}

int pm_spawn(const char *cmd, const char *workdir) {
    if (!g_pm.init)
        pm_init();
    if (g_pm.active >= PM_MAX_PROCS)
        return -1;
    int pout[2], pin[2];
    if (nc_pipe_cloexec(pout) < 0)
        return -1;
    if (nc_pipe_cloexec(pin) < 0) {
        close(pout[0]);
        close(pout[1]);
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pout[0]);
        close(pout[1]);
        close(pin[0]);
        close(pin[1]);
        return -1;
    }
    if (pid == 0) {
        nc_child_setup();
        close(pout[0]);
        close(pin[1]);
        dup2(pin[0], STDIN_FILENO);
        dup2(pout[1], STDOUT_FILENO);
        dup2(pout[1], STDERR_FILENO);
        close(pin[0]);
        close(pout[1]);
        if (workdir && workdir[0]) {
            if (chdir(workdir) != 0)
                _exit(127);
        }
        if (sandbox_enforcing() && os_sandbox_available())
            os_sandbox_apply();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(pout[1]);
    close(pin[0]);
    for (int i = 0; i < PM_MAX_PROCS; i++) {
        if (g_pm.procs[i].state == PM_FREE) {
            pm_entry_t *e = &g_pm.procs[i];
            memset(e, 0, sizeof(*e));
            e->state = PM_RUNNING;
            e->pid = pid;
            e->slot_id = g_pm.next_id++;
            nc_strlcpy(e->command, cmd ? cmd : "", PM_NAME_MAX);
            if (workdir)
                nc_strlcpy(e->workdir, workdir, NC_PATH_MAX);
            e->started = e->last_used = nc_time_now();
            e->stdin_fd = pin[1];
            e->stdout_fd = pout[0];
            g_pm.active++;
            return e->slot_id;
        }
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    close(pout[0]);
    close(pin[1]);
    return -1;
}

int pm_write_stdin(int id, const char *data, int len) {
    pm_entry_t *e = pm_find(id);
    if (!e || e->state != PM_RUNNING || e->stdin_fd <= 0)
        return -1;
    e->last_used = nc_time_now();
    return (int)write(e->stdin_fd, data, len);
}

int pm_read_stdout(int id, char *buf, int cap) {
    pm_entry_t *e = pm_find(id);
    if (!e || e->stdout_fd <= 0)
        return -1;
    e->last_used = nc_time_now();

    struct pollfd pfd = {.fd = e->stdout_fd, .events = POLLIN};
    int pr = poll(&pfd, 1, PM_YIELD_MIN_MS);
    if (pr <= 0 || !(pfd.revents & POLLIN)) {
        buf[0] = '\0';
        return 0;
    }

    int r = (int)read(e->stdout_fd, buf, cap - 1);
    if (r > 0)
        buf[r] = '\0';
    else
        buf[0] = '\0';
    return r > 0 ? r : 0;
}

int pm_kill(int id, int sig) {
    pm_entry_t *e = pm_find(id);
    if (!e || e->state != PM_RUNNING)
        return -1;
    return kill(e->pid, sig);
}

int pm_wait(int id) {
    pm_entry_t *e = pm_find(id);
    if (!e)
        return -1;
    int st;
    waitpid(e->pid, &st, 0);
    e->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    e->state = PM_EXITED;
    if (e->stdin_fd > 0) {
        close(e->stdin_fd);
        e->stdin_fd = 0;
    }
    if (e->stdout_fd > 0) {
        close(e->stdout_fd);
        e->stdout_fd = 0;
    }
    return e->exit_code;
}

int pm_check(int id) {
    pm_entry_t *e = pm_find(id);
    if (!e)
        return -1;
    if (e->state != PM_RUNNING)
        return e->exit_code;
    int st;
    pid_t r = waitpid(e->pid, &st, WNOHANG);
    if (r == 0)
        return -2;
    e->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    e->state = PM_EXITED;
    if (e->stdin_fd > 0) {
        close(e->stdin_fd);
        e->stdin_fd = 0;
    }
    if (e->stdout_fd > 0) {
        close(e->stdout_fd);
        e->stdout_fd = 0;
    }
    return e->exit_code;
}

void pm_release(int id) {
    pm_entry_t *e = pm_find(id);
    if (!e)
        return;
    if (e->state == PM_RUNNING) {
        kill(e->pid, SIGTERM);
        int st;
        pid_t r = waitpid(e->pid, &st, WNOHANG);
        if (r == 0) {
            usleep(PROC_POLL_INTERVAL_US);
            r = waitpid(e->pid, &st, WNOHANG);
        }
        if (r == 0) {
            kill(e->pid, SIGKILL);
            waitpid(e->pid, NULL, 0);
        }
    }
    if (e->stdin_fd > 0)
        close(e->stdin_fd);
    if (e->stdout_fd > 0)
        close(e->stdout_fd);
    memset(e, 0, sizeof(*e));
    if (g_pm.active > 0)
        g_pm.active--;
}

int pm_active(void) {
    return g_pm.init ? g_pm.active : 0;
}
bool pm_at_warning(void) {
    return g_pm.active >= PM_WARN_PROCS;
}
bool pm_is_running(int id) {
    pm_entry_t *e = pm_find(id);
    return e && e->state == PM_RUNNING;
}
int pm_exit_code(int id) {
    pm_entry_t *e = pm_find(id);
    return e ? e->exit_code : -1;
}
const char *pm_command(int id) {
    pm_entry_t *e = pm_find(id);
    return e ? e->command : NULL;
}

int pm_list_active(int *ids, int cap) {
    int n = 0;
    for (int i = 0; i < PM_MAX_PROCS && n < cap; i++)
        if (g_pm.procs[i].state == PM_RUNNING)
            ids[n++] = g_pm.procs[i].slot_id;
    return n;
}

void pm_cleanup_expired(int max_age_s) {
    time_t now = nc_time_now();
    for (int i = 0; i < PM_MAX_PROCS; i++) {
        pm_entry_t *e = &g_pm.procs[i];
        if (e->state == PM_RUNNING && difftime(now, e->last_used) > max_age_s)
            pm_release(e->slot_id);
        if (e->state == PM_EXITED)
            pm_release(e->slot_id);
    }
}

int pm_clamp_yield(int ms) {
    if (ms < PM_YIELD_MIN_MS)
        return PM_YIELD_MIN_MS;
    if (ms > PM_YIELD_MAX_MS)
        return PM_YIELD_MAX_MS;
    return ms;
}

void pm_free(void) {
    pm_cleanup_expired(0);
    memset(&g_pm, 0, sizeof(g_pm));
}
