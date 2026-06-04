/*
 * fork_backend.c — Process spawning via fork/exec
 *
 * Provides a subprocess interface with optional pipe-based I/O,
 * poll-based timeout waiting, and a convenience wrapper for
 * one-shot shell command execution.
 */

#include "nevercode.h"
#include <sys/ioctl.h>
#include <sys/select.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

static void close_safe(int fd) {
    if (fd >= 0)
        close(fd);
}

int fork_spawn(const fork_request_t *req, fork_child_t *child) {
    if (!req || !req->program || !child)
        return -1;

    memset(child, 0, sizeof(*child));
    child->stdin_fd = -1;
    child->stdout_fd = -1;
    child->stderr_fd = -1;
    child->exit_code = -1;

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    bool merged = (req->stdio_mode == FORK_STDIO_PIPE_MERGED);

    if (req->stdio_mode == FORK_STDIO_PIPE || merged) {
        if (nc_pipe_cloexec(out_pipe) < 0)
            return -1;
        if (!merged && nc_pipe_cloexec(err_pipe) < 0) {
            close_safe(out_pipe[0]);
            close_safe(out_pipe[1]);
            return -1;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        close_safe(out_pipe[0]);
        close_safe(out_pipe[1]);
        close_safe(err_pipe[0]);
        close_safe(err_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        if (req->new_session)
            setsid();

#ifdef __linux__
        if (req->parent_death_sig || req->kill_on_parent_exit)
            prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif

        if (req->cwd && req->cwd[0] && chdir(req->cwd) < 0)
            _exit(126);

        if (req->stdio_mode == FORK_STDIO_PIPE || merged) {
            int nfd = nc_open_devnull(O_RDONLY);
            if (nfd >= 0) {
                dup2(nfd, STDIN_FILENO);
                close(nfd);
            }
            close(out_pipe[0]);
            dup2(out_pipe[1], STDOUT_FILENO);
            if (merged) {
                dup2(out_pipe[1], STDERR_FILENO);
            } else {
                close(err_pipe[0]);
                dup2(err_pipe[1], STDERR_FILENO);
                close(err_pipe[1]);
            }
            close(out_pipe[1]);
        } else if (req->stdio_mode == FORK_STDIO_NULL) {
            int nfd = nc_open_devnull(O_RDWR);
            if (nfd >= 0) {
                dup2(nfd, STDIN_FILENO);
                dup2(nfd, STDOUT_FILENO);
                dup2(nfd, STDERR_FILENO);
                close(nfd);
            }
        }

        if (sandbox_enforcing() && os_sandbox_available())
            os_sandbox_apply();

        int total = req->argc + 2;
        const char **av = calloc(total, sizeof(char *));
        if (!av)
            _exit(127);
        av[0] = req->arg0 ? req->arg0 : req->program;
        for (int i = 0; i < req->argc; i++)
            av[i + 1] = req->args[i];
        av[req->argc + 1] = NULL;

        execvp(req->program, (char *const *)av);
        _exit(127);
    }

    /* Parent process */
    child->pid = pid;
    if (req->stdio_mode == FORK_STDIO_PIPE || merged) {
        close(out_pipe[1]);
        child->stdout_fd = out_pipe[0];
        if (!merged) {
            close(err_pipe[1]);
            child->stderr_fd = err_pipe[0];
        }
    }
    return 0;
}

int fork_wait(fork_child_t *child, int timeout_ms) {
    if (!child || child->pid <= 0)
        return -1;
    if (child->exited)
        return 0;

    if (timeout_ms <= 0) {
        int st = 0;
        if (waitpid(child->pid, &st, 0) < 0)
            return -1;
        child->exited = true;
        if (WIFEXITED(st))
            child->exit_code = WEXITSTATUS(st);
        else if (WIFSIGNALED(st)) {
            child->signaled = true;
            child->term_signal = WTERMSIG(st);
            child->exit_code = NC_EXIT_SIGNAL(child->term_signal);
        }
        return 0;
    }

    long waited = 0;
    int poll_ms = FORK_POLL_INITIAL_MS;
    while (waited < timeout_ms) {
        int st = 0;
        pid_t r = waitpid(child->pid, &st, WNOHANG);
        if (r > 0) {
            child->exited = true;
            if (WIFEXITED(st))
                child->exit_code = WEXITSTATUS(st);
            else if (WIFSIGNALED(st)) {
                child->signaled = true;
                child->term_signal = WTERMSIG(st);
                child->exit_code = NC_EXIT_SIGNAL(child->term_signal);
            }
            return 0;
        }
        if (r < 0 && errno != EINTR)
            return -1;
        usleep(poll_ms * 1000);
        waited += poll_ms;
        if (poll_ms < FORK_POLL_MAX_MS)
            poll_ms *= 2;
    }
    return 1;
}

int fork_kill(fork_child_t *child, int sig) {
    if (!child || child->pid <= 0 || child->exited)
        return -1;
    return kill(child->pid, sig);
}

static int fork_read_timed(int fd, strbuf_t *sb, size_t max, int timeout_ms) {
    if (fd < 0 || fd >= FD_SETSIZE)
        return -1;
    size_t total = 0;
    char buf[NC_IO_BUF];
    long deadline_ms = nc_mono_ms() + timeout_ms;

    for (;;) {
        long remain = deadline_ms - nc_mono_ms();
        if (remain <= 0)
            break;

        struct timeval tv;
        tv.tv_sec = remain / 1000;
        tv.tv_usec = (remain % 1000) * 1000;

        fd_set fds;
        FD_ZERO(&fds);
        if (fd >= FD_SETSIZE)
            break;
        FD_SET(fd, &fds);
        int sr = select(fd + 1, &fds, NULL, NULL, &tv);
        if (sr <= 0)
            break;

        ssize_t r = read(fd, buf, sizeof(buf));
        if (r <= 0)
            break;
        if (total + (size_t)r > max) {
            strbuf_append(sb, buf, max - total);
            break;
        }
        strbuf_append(sb, buf, (size_t)r);
        total += (size_t)r;
    }
    return (int)total;
}

int fork_read_output(fork_child_t *child, char **out, size_t max) {
    if (!child || !out || child->stdout_fd < 0)
        return -1;

    strbuf_t sb;
    strbuf_init(&sb);
    char buf[NC_IO_BUF];
    size_t total = 0;

    for (;;) {
        ssize_t r = read(child->stdout_fd, buf, sizeof(buf));
        if (r <= 0)
            break;
        if (total + (size_t)r > max) {
            strbuf_append(&sb, buf, max - total);
            break;
        }
        strbuf_append(&sb, buf, (size_t)r);
        total += (size_t)r;
    }

    *out = strbuf_detach(&sb);
    return 0;
}

void fork_child_cleanup(fork_child_t *child) {
    if (!child)
        return;
    close_safe(child->stdin_fd);
    close_safe(child->stdout_fd);
    close_safe(child->stderr_fd);
    child->stdin_fd = -1;
    child->stdout_fd = -1;
    child->stderr_fd = -1;
}

int fork_exec_simple(const char *cmd, const char *cwd, int timeout_ms, char **output,
                     int *exit_code) {
    if (!cmd)
        return -1;

    const char *args[] = {"-c", cmd};
    fork_request_t req;
    memset(&req, 0, sizeof(req));
    req.program = "/bin/sh";
    req.args = args;
    req.argc = 2;
    req.cwd = cwd;
    req.stdio_mode = FORK_STDIO_PIPE_MERGED;
    req.detach_tty = true;
    req.parent_death_sig = true;

    fork_child_t child;
    if (fork_spawn(&req, &child) < 0)
        return -1;

    strbuf_t outbuf;
    strbuf_init(&outbuf);

    nc_set_nonblock(child.stdout_fd);

    int cmd_timeout = timeout_ms > 0 ? timeout_ms : DEFAULT_BASH_TIMEOUT * 1000;
    fork_read_timed(child.stdout_fd, &outbuf, SHELL_OUTPUT_MAX_BYTES, cmd_timeout);

    int rc = fork_wait(&child, PROC_KILL_WAIT_MS);
    if (rc == 1) {
        fork_kill(&child, SIGTERM);
        usleep(PROC_POLL_INTERVAL_US);
        if (fork_wait(&child, PROC_KILL_WAIT_MS) != 0) {
            fork_kill(&child, SIGKILL);
            fork_wait(&child, PROC_KILL_WAIT_MS);
        }
    }

    if (outbuf.len < SHELL_OUTPUT_MAX_BYTES && child.stdout_fd >= 0)
        fork_read_timed(child.stdout_fd, &outbuf, SHELL_OUTPUT_MAX_BYTES, IO_DRAIN_TIMEOUT_MS);

    if (output)
        *output = strbuf_detach(&outbuf);
    else
        strbuf_free(&outbuf);

    if (exit_code)
        *exit_code = child.exit_code;
    fork_child_cleanup(&child);
    return 0;
}
