/*
 * shell_session.c — Persistent shell session for BashTool
 *
 * Keeps a single shell process alive between BashTool invocations so
 * environment variables, aliases, and cwd persist across calls.
 *
 * Protocol:
 *   1. Write command to shell stdin, bracketed by unique markers
 *   2. Read stdout/stderr until end marker appears
 *   3. Parse exit code from the marker line
 */

#include "nevercode.h"
#include <poll.h>

#define SHELL_IDLE_TIMEOUT_S SHELL_SESSION_IDLE_TIMEOUT
#define SHELL_MARKER_PREFIX SHELL_SESSION_MARKER_PREFIX
#define SHELL_READ_BUF SHELL_SESSION_READ_BUF

typedef struct {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    time_t last_used;
    int alive;
} shell_session_t;

static shell_session_t g_shell = {0, -1, -1, 0, 0};

static void shell_session_kill(shell_session_t *s) {
    if (!s->alive)
        return;
    if (s->stdin_fd >= 0)
        close(s->stdin_fd);
    if (s->stdout_fd >= 0)
        close(s->stdout_fd);
    s->stdin_fd = -1;
    s->stdout_fd = -1;
    if (s->pid > 0) {
        kill(s->pid, SIGTERM);
        usleep(PROC_POLL_FAST_US);
        int status;
        if (waitpid(s->pid, &status, WNOHANG) == 0) {
            kill(s->pid, SIGKILL);
            waitpid(s->pid, &status, 0);
        }
    }
    s->pid = 0;
    s->alive = 0;
}

static int shell_session_spawn(shell_session_t *s) {
    shell_session_kill(s);

    signal(SIGPIPE, SIG_IGN);

    int pipe_in[2], pipe_out[2];
    if (nc_pipe_cloexec(pipe_in) < 0)
        return -1;
    if (nc_pipe_cloexec(pipe_out) < 0) {
        close(pipe_in[0]);
        close(pipe_in[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_in[0]);
        close(pipe_in[1]);
        close(pipe_out[0]);
        close(pipe_out[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipe_in[1]);
        close(pipe_out[0]);
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_out[1], STDERR_FILENO);
        close(pipe_in[0]);
        close(pipe_out[1]);
        setpgid(0, 0);
        setenv("PS1", "", 1);
        setenv("PS2", "", 1);
        if (sandbox_enforcing() && os_sandbox_available())
            os_sandbox_apply();
        execl("/bin/sh", "sh", (char *)NULL);
        _exit(127);
    }

    close(pipe_in[0]);
    close(pipe_out[1]);
    setpgid(pid, pid);

    nc_set_nonblock(pipe_out[0]);

    s->pid = pid;
    s->stdin_fd = pipe_in[1];
    s->stdout_fd = pipe_out[0];
    s->last_used = nc_time_now();
    s->alive = 1;

    usleep(PROC_POLL_FAST_US);

    return 0;
}

static int shell_is_alive(shell_session_t *s) {
    if (!s->alive || s->pid <= 0)
        return 0;
    int status;
    pid_t r = waitpid(s->pid, &status, WNOHANG);
    if (r > 0) {
        s->alive = 0;
        if (s->stdin_fd >= 0)
            close(s->stdin_fd);
        if (s->stdout_fd >= 0)
            close(s->stdout_fd);
        s->stdin_fd = -1;
        s->stdout_fd = -1;
        return 0;
    }
    return 1;
}

void shell_session_init(void) {
    memset(&g_shell, 0, sizeof(g_shell));
    g_shell.stdin_fd = -1;
    g_shell.stdout_fd = -1;
}

void shell_session_cleanup(void) {
    shell_session_kill(&g_shell);
}

int shell_session_exec(const char *cmd, const char *workdir, int timeout_s, char **output,
                       int *exit_code) {
    if (!cmd || !output || !exit_code)
        return -1;

    *output = NULL;
    *exit_code = -1;

    if (g_shell.alive && nc_time_now() - g_shell.last_used > SHELL_IDLE_TIMEOUT_S)
        shell_session_kill(&g_shell);

    if (!shell_is_alive(&g_shell)) {
        if (shell_session_spawn(&g_shell) < 0)
            return -1;
    }

    g_shell.last_used = nc_time_now();

    char marker[NC_FMT_MAX];
    snprintf(marker, sizeof(marker), SHELL_MARKER_PREFIX "%ld_%u", nc_wall_sec(),
             nc_rand_uniform(10000));

    strbuf_t cmdbuf;
    strbuf_init(&cmdbuf);

    if (workdir && *workdir) {
        strbuf_append_str(&cmdbuf, "cd ");
        strbuf_append_str(&cmdbuf, workdir);
        strbuf_append_str(&cmdbuf, " 2>/dev/null\n");
    }

    strbuf_append_str(&cmdbuf, cmd);
    strbuf_append_str(&cmdbuf, "\n");

    /* Write exit code and marker to stdout so we can parse it.
     * Use a subshell to capture $? reliably */
    strbuf_append_str(&cmdbuf, "__rc=$?\nprintf '");
    strbuf_append_str(&cmdbuf, marker);
    strbuf_append_str(&cmdbuf, ":%d\\n' $__rc\n");

    signal(SIGPIPE, SIG_IGN);
    ssize_t w = write(g_shell.stdin_fd, cmdbuf.data, cmdbuf.len);
    strbuf_free(&cmdbuf);

    if (w < 0) {
        shell_session_kill(&g_shell);
        return -1;
    }

    /* Read output until we see the marker */
    strbuf_t out;
    strbuf_init(&out);

    time_t deadline = nc_time_now() + (timeout_s > 0 ? timeout_s : 30);
    char readbuf[SHELL_READ_BUF];
    int found_marker = 0;
    int parsed_rc = 0;

    while (!found_marker && nc_time_now() < deadline) {
        struct pollfd pfd = {g_shell.stdout_fd, POLLIN, 0};
        int pr = poll(&pfd, 1, SHELL_SESSION_POLL_MS);
        if (pr < 0 && errno == EINTR)
            continue;
        if (pr <= 0) {
            if (!shell_is_alive(&g_shell))
                break;
            continue;
        }

        ssize_t n = read(g_shell.stdout_fd, readbuf, sizeof(readbuf) - 1);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            break;
        }
        readbuf[n] = '\0';
        strbuf_append(&out, readbuf, (size_t)n);

        /* Check for marker in accumulated output */
        char *mp = strstr(out.data, marker);
        if (mp) {
            found_marker = 1;
            char *colon = mp + strlen(marker);
            if (*colon == ':') {
                parsed_rc = nc_safe_atoi(colon + 1, -1);
            }
            /* Truncate output at marker */
            *mp = '\0';
            out.len = (size_t)(mp - out.data);
            /* Strip trailing newline before marker */
            while (out.len > 0 && (out.data[out.len - 1] == '\n' || out.data[out.len - 1] == '\r'))
                out.data[--out.len] = '\0';
        }

        if (out.len > BASH_TOOL_OUTPUT_MAX)
            break;
    }

    if (!found_marker) {
        /* Timeout or shell died — kill and return what we have */
        shell_session_kill(&g_shell);
        *exit_code = NC_EXIT_TIMEOUT;
    } else {
        *exit_code = parsed_rc;
    }

    *output = strbuf_detach(&out);
    return 0;
}

bool shell_session_active(void) {
    return g_shell.alive && shell_is_alive(&g_shell);
}

void shell_session_reset(void) {
    shell_session_kill(&g_shell);
}
