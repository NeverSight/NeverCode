/*
 * async_watcher.c — poll child processes and collect pipe output
 */

#include "nevercode.h"
#include <poll.h>

typedef struct {
    bool used;
    pid_t pid;
    int out;
    int err;
    long t0;
    long deadline;
    char label[NC_FMT_MAX];
    strbuf_t acc;
    bool done;
    int code;
    bool timed_out;
} slot_t;

static struct {
    slot_t slots[MAX_ASYNC_WATCHED];
    int total_added;
} g;

static void drain_fd(int fd, strbuf_t *acc) {
    if (fd < 0 || !acc)
        return;
    char buf[NC_IO_BUF];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0)
            strbuf_append(acc, buf, (size_t)n);
        else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN))
            break;
    }
}

void async_watcher_init(void) {
    memset(&g, 0, sizeof g);
}

void async_watcher_free(void) {
    for (int i = 0; i < MAX_ASYNC_WATCHED; i++) {
        if (g.slots[i].used) {
            if (g.slots[i].out >= 0)
                close(g.slots[i].out);
            if (g.slots[i].err >= 0)
                close(g.slots[i].err);
            strbuf_free(&g.slots[i].acc);
        }
    }
    memset(&g, 0, sizeof g);
}

int async_watcher_add(pid_t pid, int stdout_fd, int stderr_fd, long timeout_ms, const char *label) {
    if (pid <= 0)
        return -1;
    for (int i = 0; i < MAX_ASYNC_WATCHED; i++) {
        if (!g.slots[i].used) {
            slot_t *s = &g.slots[i];
            strbuf_free(&s->acc);
            memset(s, 0, sizeof *s);
            s->used = true;
            s->pid = pid;
            s->out = stdout_fd;
            s->err = stderr_fd;
            s->t0 = nc_mono_ms();
            s->deadline = timeout_ms > 0 ? s->t0 + timeout_ms : 0;
            if (label)
                nc_strlcpy(s->label, label, sizeof s->label);
            strbuf_init(&s->acc);
            nc_set_nonblock(s->out);
            nc_set_nonblock(s->err);
            g.total_added++;
            return i;
        }
    }
    return -1;
}

static void finalize_slot(slot_t *s, int status) {
    drain_fd(s->out, &s->acc);
    drain_fd(s->err, &s->acc);
    if (s->out >= 0) {
        close(s->out);
        s->out = -1;
    }
    if (s->err >= 0) {
        close(s->err);
        s->err = -1;
    }
    s->done = true;
    if (WIFEXITED(status))
        s->code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        s->code = NC_EXIT_SIGNAL(WTERMSIG(status));
    else
        s->code = -1;
}

int async_watcher_poll(void) {
    int finished = 0;
    for (int i = 0; i < MAX_ASYNC_WATCHED; i++) {
        slot_t *s = &g.slots[i];
        if (!s->used || s->done)
            continue;

        long t = nc_mono_ms();
        if (s->deadline > 0 && t >= s->deadline && !s->timed_out) {
            s->timed_out = true;
            kill(s->pid, SIGTERM);
        }

        struct pollfd pf[2];
        int np = 0;
        if (s->out >= 0) {
            pf[np].fd = s->out;
            pf[np].events = POLLIN;
            np++;
        }
        if (s->err >= 0) {
            pf[np].fd = s->err;
            pf[np].events = POLLIN;
            np++;
        }
        if (np > 0)
            poll(pf, (unsigned)np, 0);

        drain_fd(s->out, &s->acc);
        drain_fd(s->err, &s->acc);

        int st;
        pid_t w = waitpid(s->pid, &st, WNOHANG);
        if (w == s->pid) {
            finalize_slot(s, st);
            finished++;
            s->used = false;
        } else if (w < 0 && errno == ECHILD) {
            finalize_slot(s, 0);
            finished++;
            s->used = false;
        }
    }
    return finished;
}

bool async_watcher_done(int idx) {
    if (idx < 0 || idx >= MAX_ASYNC_WATCHED)
        return true;
    return !g.slots[idx].used || g.slots[idx].done;
}

int async_watcher_exit_code(int idx) {
    if (idx < 0 || idx >= MAX_ASYNC_WATCHED)
        return -1;
    if (!g.slots[idx].done)
        return -1;
    return g.slots[idx].code;
}

char *async_watcher_output(int idx) {
    if (idx < 0 || idx >= MAX_ASYNC_WATCHED || !g.slots[idx].done)
        return NULL;
    const char *p = g.slots[idx].acc.data;
    return p ? nc_xstrdup(p) : nc_xstrdup("");
}

int async_watcher_active_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_ASYNC_WATCHED; i++)
        if (g.slots[i].used && !g.slots[i].done)
            n++;
    return n;
}
