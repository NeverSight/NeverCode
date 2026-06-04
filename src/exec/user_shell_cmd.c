/*
 * user_shell_cmd.c — User shell command execution with turn modes.
 *
 * Supports standalone turn (emits turn start/complete) and auxiliary
 * mode (runs within an active turn). Handles timeout, output capture,
 * and sandbox policy integration.
 */
#include "nevercode.h"
#include "defaults.h"

typedef enum {
    USC_MODE_STANDALONE,
    USC_MODE_AUXILIARY,
} usc_mode_t;

typedef struct {
    char command[NC_CMD_MAX];
    usc_mode_t mode;
    int exit_code;
    long elapsed_ms;
    char *output;
    size_t output_len;
    bool timed_out;
    bool completed;
} usc_result_t;

static struct {
    int total_commands;
    int total_failures;
    int total_timeouts;
    long total_elapsed_ms;
} g_usc_stats;

int usc_execute(const char *command, usc_mode_t mode, int timeout_ms, usc_result_t *result) {
    if (!command || !result)
        return -1;
    memset(result, 0, sizeof(*result));
    nc_strlcpy(result->command, command, sizeof(result->command));
    result->mode = mode;

    int pout[2];
    if (nc_pipe_cloexec(pout) != 0)
        return -1;

    long t0_ms = nc_mono_ms();

    pid_t pid = fork();
    if (pid < 0) {
        close(pout[0]);
        close(pout[1]);
        return -1;
    }

    if (pid == 0) {
        close(pout[0]);
        dup2(pout[1], STDOUT_FILENO);
        dup2(pout[1], STDERR_FILENO);
        close(pout[1]);
        if (sandbox_enforcing() && os_sandbox_available())
            os_sandbox_apply();
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    close(pout[1]);
    strbuf_t out;
    strbuf_init(&out);
    char buf[NC_IO_BUF];
    int timeout_s = timeout_ms > 0 ? timeout_ms / 1000 : USC_DEFAULT_TIMEOUT_S;
    if (timeout_s < 1)
        timeout_s = 1;
    time_t deadline = nc_time_now() + timeout_s;

    while (1) {
        if (nc_time_now() >= deadline) {
            result->timed_out = true;
            kill(pid, SIGTERM);
            break;
        }
        fd_set rfds;
        struct timeval tv = {PROC_SELECT_POLL_SEC, 0};
        FD_ZERO(&rfds);
        if (pout[0] >= FD_SETSIZE)
            break;
        FD_SET(pout[0], &rfds);
        int sel = select(pout[0] + 1, &rfds, NULL, NULL, &tv);
        if (sel > 0) {
            ssize_t n = read(pout[0], buf, sizeof(buf) - 1);
            if (n <= 0)
                break;
            buf[n] = '\0';
            if (out.len < USC_MAX_OUTPUT)
                strbuf_append(&out, buf, (size_t)n);
        } else if (sel == 0) {
            int st;
            if (waitpid(pid, &st, WNOHANG) == pid) {
                ssize_t n;
                while ((n = read(pout[0], buf, sizeof(buf) - 1)) > 0) {
                    buf[n] = '\0';
                    if (out.len < USC_MAX_OUTPUT)
                        strbuf_append(&out, buf, (size_t)n);
                }
                result->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
                result->completed = true;
                goto done;
            }
        }
    }

    {
        int st;
        waitpid(pid, &st, 0);
        result->exit_code = result->timed_out ? -2 : (WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        result->completed = !result->timed_out;
    }

done:
    close(pout[0]);
    result->elapsed_ms = nc_mono_ms() - t0_ms;

    if (out.len > 0) {
        result->output = strbuf_detach(&out);
        result->output_len = strlen(result->output);
    } else {
        strbuf_free(&out);
    }

    g_usc_stats.total_commands++;
    g_usc_stats.total_elapsed_ms += result->elapsed_ms;
    if (result->exit_code != 0)
        g_usc_stats.total_failures++;
    if (result->timed_out)
        g_usc_stats.total_timeouts++;

    return result->exit_code;
}

void usc_result_free(usc_result_t *r) {
    if (r) {
        free(r->output);
        r->output = NULL;
    }
}

int usc_total_commands(void) {
    return g_usc_stats.total_commands;
}

char *usc_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "UserShell: %d commands, %d failures, %d timeouts, %ldms total",
                   g_usc_stats.total_commands, g_usc_stats.total_failures,
                   g_usc_stats.total_timeouts, g_usc_stats.total_elapsed_ms);
    return strbuf_detach(&sb);
}
