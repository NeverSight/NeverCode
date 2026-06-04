/*
 * unified_exec.c — Unified execution handler
 *
 * Provides a single entry point for all command execution with support for:
 * - Shell selection (bash, zsh, sh, etc.)
 * - Working directory management
 * - TTY / pseudo-terminal allocation
 * - Yield-based output collection
 * - Integration with the process manager and sandbox
 */

#include "repl.h"
#include "tools.h"
#include "nevercode.h"

/* Macros sourced from defaults.h: UEXEC_DEFAULT_YIELD_MS, UEXEC_BUF_SIZE */
#define UEXEC_DEFAULT_TTY true

typedef struct {
    char *cmd;
    char *workdir;
    char *shell;
    bool login_shell;
    bool tty;
    int yield_ms;
    int max_output_tokens;
    int timeout_s;
} unified_exec_request_t;

typedef struct {
    int exit_code;
    char *stdout_buf;
    char *stderr_buf;
    int stdout_len;
    int stderr_len;
    bool timed_out;
    bool killed;
    int elapsed_ms;
    int pid;
} unified_exec_result_t;

void uexec_request_init(unified_exec_request_t *req) {
    memset(req, 0, sizeof(*req));
    req->tty = UEXEC_DEFAULT_TTY;
    req->yield_ms = UEXEC_DEFAULT_YIELD_MS;
    req->timeout_s = DEFAULT_BASH_TIMEOUT;
}

void uexec_request_free(unified_exec_request_t *req) {
    if (!req)
        return;
    free(req->cmd);
    free(req->workdir);
    free(req->shell);
    memset(req, 0, sizeof(*req));
}

void uexec_result_free(unified_exec_result_t *res) {
    if (!res)
        return;
    free(res->stdout_buf);
    free(res->stderr_buf);
    memset(res, 0, sizeof(*res));
}

static const char *resolve_shell(const char *requested) {
    if (requested && requested[0])
        return requested;
    const char *env = nc_shell_env();
    if (env)
        return env;
    return "/bin/sh";
}

int uexec_from_json(unified_exec_request_t *req, const cJSON *args) {
    if (!req || !args)
        return -1;

    uexec_request_init(req);

    const cJSON *cmd = cJSON_GetObjectItem(args, "cmd");
    if (!cmd || !cJSON_IsString(cmd))
        return -1;
    req->cmd = nc_xstrdup(cmd->valuestring);

    const cJSON *wd = cJSON_GetObjectItem(args, "workdir");
    if (wd && cJSON_IsString(wd))
        req->workdir = nc_xstrdup(wd->valuestring);

    const cJSON *sh = cJSON_GetObjectItem(args, "shell");
    if (sh && cJSON_IsString(sh))
        req->shell = nc_xstrdup(sh->valuestring);

    const cJSON *login = cJSON_GetObjectItem(args, "login");
    if (login)
        req->login_shell = cJSON_IsTrue(login);

    const cJSON *tty = cJSON_GetObjectItem(args, "tty");
    if (tty)
        req->tty = cJSON_IsTrue(tty);

    const cJSON *yield = cJSON_GetObjectItem(args, "yield_time_ms");
    if (yield && cJSON_IsNumber(yield))
        req->yield_ms = (int)yield->valuedouble;

    const cJSON *maxtok = cJSON_GetObjectItem(args, "max_output_tokens");
    if (maxtok && cJSON_IsNumber(maxtok))
        req->max_output_tokens = (int)maxtok->valuedouble;

    const cJSON *to = cJSON_GetObjectItem(args, "timeout");
    if (to && cJSON_IsNumber(to))
        req->timeout_s = (int)to->valuedouble;

    return 0;
}

int uexec_run(const unified_exec_request_t *req, unified_exec_result_t *res) {
    if (!req || !req->cmd || !res)
        return -1;

    memset(res, 0, sizeof(*res));

    const char *shell = resolve_shell(req->shell);

    int pipefd[2];
    if (nc_pipe_cloexec(pipefd) != 0)
        return -1;

    long t_start_ms = nc_mono_ms();

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (req->workdir && req->workdir[0]) {
            if (chdir(req->workdir) != 0)
                _exit(127);
        }

        if (sandbox_enforcing() && os_sandbox_available())
            os_sandbox_apply();

        if (req->login_shell)
            execl(shell, shell, "-l", "-c", req->cmd, (char *)NULL);
        else
            execl(shell, shell, "-c", req->cmd, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    res->pid = pid;

    strbuf_t out;
    strbuf_init(&out);

    char buf[UEXEC_BUF_SIZE];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        strbuf_append(&out, buf, (size_t)n);

        if (req->max_output_tokens > 0 && (int)out.len > req->max_output_tokens)
            break;
    }
    close(pipefd[0]);

    int status = 0;
    int wrc = waitpid(pid, &status, 0);
    if (wrc < 0 && errno == ECHILD) {
        res->exit_code = -1;
    } else if (WIFEXITED(status)) {
        res->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res->exit_code = NC_EXIT_SIGNAL(WTERMSIG(status));
        res->killed = true;
    }

    res->elapsed_ms = (int)(nc_mono_ms() - t_start_ms);

    res->stdout_buf = strbuf_detach(&out);
    res->stdout_len = res->stdout_buf ? (int)strlen(res->stdout_buf) : 0;

    return 0;
}

char *uexec_format_result(const unified_exec_result_t *res) {
    if (!res)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "exit_code=%d elapsed=%dms pid=%d", res->exit_code, res->elapsed_ms,
                   res->pid);
    if (res->timed_out)
        strbuf_append_str(&sb, " [timed_out]");
    if (res->killed)
        strbuf_append_str(&sb, " [killed]");
    strbuf_append_str(&sb, "\n");
    if (res->stdout_buf && res->stdout_len > 0)
        strbuf_append_str(&sb, res->stdout_buf);
    return strbuf_detach(&sb);
}
