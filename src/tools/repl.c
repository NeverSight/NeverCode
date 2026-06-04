#include "nevercode.h"
#include "tools.h"
#include <poll.h>

/* Limits configured in defaults.h: REPL_MAX_SESSIONS, REPL_READ_BUF_SIZE, REPL_EXEC_TIMEOUT */

typedef struct {
    char lang[NC_SHORT_MAX];
    pid_t pid;
    int fd_in;
    int fd_out;
    bool active;
} repl_session_t;

static repl_session_t g_repls[REPL_MAX_SESSIONS]; /* from defaults.h */
static int g_num_repls = 0;

static repl_session_t *repl_find(const char *lang) {
    for (int i = 0; i < g_num_repls; i++)
        if (g_repls[i].active && strcmp(g_repls[i].lang, lang) == 0)
            return &g_repls[i];
    return NULL;
}

static void repl_close_session(repl_session_t *s) {
    if (!s || !s->active)
        return;
    close(s->fd_in);
    close(s->fd_out);
    kill(s->pid, SIGTERM);
    int status;
    waitpid(s->pid, &status, WNOHANG);
    s->active = false;
}

static repl_session_t *repl_start(const char *lang) {
    if (g_num_repls >= REPL_MAX_SESSIONS) {
        for (int i = 0; i < REPL_MAX_SESSIONS; i++) {
            if (!g_repls[i].active) {
                g_num_repls = i;
                break;
            }
        }
        if (g_num_repls >= REPL_MAX_SESSIONS) {
            repl_close_session(&g_repls[0]);
            memmove(&g_repls[0], &g_repls[1], sizeof(repl_session_t) * (REPL_MAX_SESSIONS - 1));
            g_num_repls = REPL_MAX_SESSIONS - 1;
        }
    }

    const char *cmd = NULL;
    if (strcmp(lang, "python") == 0 || strcmp(lang, "python3") == 0)
        cmd = "python3 -u -i";
    else if (strcmp(lang, "node") == 0 || strcmp(lang, "javascript") == 0)
        cmd = "node -i";
    else if (strcmp(lang, "ruby") == 0)
        cmd = "irb --noecho";
    else
        return NULL;

    int pipe_in[2], pipe_out[2];
    if (nc_pipe_cloexec(pipe_in) < 0 || nc_pipe_cloexec(pipe_out) < 0)
        return NULL;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_in[0]);
        close(pipe_in[1]);
        close(pipe_out[0]);
        close(pipe_out[1]);
        return NULL;
    }

    if (pid == 0) {
        close(pipe_in[1]);
        close(pipe_out[0]);
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_out[1], STDERR_FILENO);
        close(pipe_in[0]);
        close(pipe_out[1]);
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(127);
    }

    close(pipe_in[0]);
    close(pipe_out[1]);

    nc_set_nonblock(pipe_out[0]);

    repl_session_t *s = &g_repls[g_num_repls++];
    nc_strlcpy(s->lang, lang, sizeof(s->lang));
    s->pid = pid;
    s->fd_in = pipe_in[1];
    s->fd_out = pipe_out[0];
    s->active = true;

    usleep(PROC_TERM_GRACE_US);

    return s;
}

static char *repl_read_output(repl_session_t *s, int timeout_ms) {
    strbuf_t sb;
    strbuf_init(&sb);
    char buf[REPL_READ_BUF_SIZE];

    struct pollfd pfd = {.fd = s->fd_out, .events = POLLIN};
    int elapsed = 0;
    int poll_interval = REPL_POLL_INTERVAL_MS;

    while (elapsed < timeout_ms) {
        int rc = poll(&pfd, 1, poll_interval);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(s->fd_out, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                strbuf_append(&sb, buf, n);
                elapsed = 0;
            } else if (n == 0) {
                break;
            }
        } else {
            elapsed += poll_interval;
            if (sb.len > 0 && elapsed > REPL_OUTPUT_FLUSH_MS)
                break;
        }
    }

    if (sb.len == 0) {
        strbuf_free(&sb);
        return nc_xstrdup("(no output)\n");
    }
    return strbuf_detach(&sb);
}

int repl_tool_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;

    const char *lang = nc_cjson_str((cJSON *)input, "language", NULL);
    const char *code = nc_cjson_str((cJSON *)input, "code", NULL);
    const char *action = nc_cjson_str((cJSON *)input, "action", NULL);

    if (action && strcmp(action, "stop") == 0) {
        if (!lang) {
            *output = nc_xstrdup("error: 'language' required for stop");
            return 1;
        }
        repl_session_t *s = repl_find(lang);
        if (s) {
            repl_close_session(s);
            *output = nc_xstrdup("REPL session stopped.");
        } else {
            *output = nc_xstrdup("No active session for this language.");
        }
        return 0;
    }

    if (!lang || !code) {
        *output = nc_xstrdup("error: 'language' and 'code' are required");
        return 1;
    }

    repl_session_t *s = repl_find(lang);
    if (!s) {
        s = repl_start(lang);
        if (!s) {
            char err[NC_LABEL_MAX];
            snprintf(err, sizeof(err), "error: failed to start %s REPL", lang);
            *output = nc_xstrdup(err);
            return 1;
        }
        char *init_out = repl_read_output(s, PROC_INIT_WAIT_MS);
        free(init_out);
    }

    int wstatus = 0;
    pid_t wr = waitpid(s->pid, &wstatus, WNOHANG);
    if (wr > 0) {
        s->active = false;
        s = repl_start(lang);
        if (!s) {
            *output = nc_xstrdup("error: REPL session died and could not restart");
            return 1;
        }
        char *init_out = repl_read_output(s, PROC_INIT_WAIT_MS);
        free(init_out);
    }

    size_t code_len = strlen(code);
    strbuf_t cmd;
    strbuf_init(&cmd);
    strbuf_append_str(&cmd, code);
    if (code_len == 0 || code[code_len - 1] != '\n')
        strbuf_append_str(&cmd, "\n");

    size_t total = cmd.len;
    size_t sent = 0;
    while (sent < total) {
        ssize_t n = write(s->fd_in, cmd.data + sent, total - sent);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            strbuf_free(&cmd);
            *output = nc_xstrdup("error: failed to write to REPL stdin");
            return 1;
        }
        sent += (size_t)n;
    }
    strbuf_free(&cmd);

    cJSON *timeout_obj = cJSON_GetObjectItem((cJSON *)input, "timeout");
    int timeout_s = REPL_EXEC_TIMEOUT;
    if (timeout_obj && cJSON_IsNumber(timeout_obj))
        timeout_s = timeout_obj->valueint;
    if (timeout_s < 1)
        timeout_s = 1;
    if (timeout_s > REPL_MAX_TIMEOUT)
        timeout_s = REPL_MAX_TIMEOUT;

    *output = repl_read_output(s, timeout_s * 1000);
    return 0;
}

void repl_cleanup_all(void) {
    for (int i = 0; i < g_num_repls; i++)
        repl_close_session(&g_repls[i]);
    g_num_repls = 0;
}

int repl_active_count(void) {
    int count = 0;
    for (int i = 0; i < g_num_repls; i++)
        if (g_repls[i].active)
            count++;
    return count;
}

void tool_repl_register(void) {
    static cJSON *schema = NULL;
    if (!schema) {
        schema = cJSON_CreateObject();
        cJSON_AddStringToObject(schema, "type", "object");
        cJSON *props = cJSON_AddObjectToObject(schema, "properties");

        cJSON *lang = cJSON_AddObjectToObject(props, "language");
        cJSON_AddStringToObject(lang, "type", "string");
        cJSON_AddStringToObject(lang, "description", "Programming language: python, node, ruby");

        cJSON *code = cJSON_AddObjectToObject(props, "code");
        cJSON_AddStringToObject(code, "type", "string");
        cJSON_AddStringToObject(code, "description", "Code to execute in the REPL session");

        cJSON *action = cJSON_AddObjectToObject(props, "action");
        cJSON_AddStringToObject(action, "type", "string");
        cJSON_AddStringToObject(action, "description", "Optional: 'stop' to close the session");

        cJSON *timeout = cJSON_AddObjectToObject(props, "timeout");
        cJSON_AddStringToObject(timeout, "type", "integer");
        cJSON_AddStringToObject(timeout, "description", "Timeout in seconds (default 30, max 300)");

        cJSON *req = cJSON_CreateArray();
        cJSON_AddItemToArray(req, cJSON_CreateString("language"));
        cJSON_AddItemToObject(schema, "required", req);
    }

    tool_def_t t = {.name = "REPL",
                    .description = "Execute code in a persistent REPL session "
                                   "(Python/Node/Ruby). State persists across calls.",
                    .input_schema = schema,
                    .execute = repl_tool_exec,
                    .check_permission = NULL,
                    .read_only = false,
                    .concurrency_safe = false,
                    .destructive = false};
    tools_register(&t);
}

char *repl_list_sessions(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    int active = 0;
    for (int i = 0; i < g_num_repls; i++) {
        if (!g_repls[i].active)
            continue;
        active++;
        char line[NC_TAG_MAX];
        snprintf(line, sizeof(line), "  [%s] pid=%d\n", g_repls[i].lang, (int)g_repls[i].pid);
        strbuf_append_str(&sb, line);
    }

    if (active == 0) {
        strbuf_free(&sb);
        return nc_xstrdup("No active REPL sessions.\n");
    }

    char header[NC_FMT_MAX];
    snprintf(header, sizeof(header), "Active REPL sessions (%d):\n", active);

    strbuf_t result;
    strbuf_init(&result);
    strbuf_append_str(&result, header);
    strbuf_append_str(&result, sb.data);
    strbuf_free(&sb);
    return strbuf_detach(&result);
}
