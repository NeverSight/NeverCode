#include "nevercode.h"
#include "config.h"
#include "tools.h"
#include <poll.h>

#define AGENT_OUTPUT_MAX AGENT_OUTPUT_MAX_BYTES

static agent_pool_t g_pool;
static int g_pool_initialized = 0;

static char *gen_agent_id(void) {
    static int counter = 0;
    char buf[NC_SHORT_MAX];
    snprintf(buf, sizeof(buf), "agent-%d-%ld", ++counter, nc_wall_sec() % 10000);
    return nc_xstrdup(buf);
}

void agent_pool_init(agent_pool_t *p) {
    memset(p, 0, sizeof(*p));
}

void agent_pool_free(agent_pool_t *p) {
    for (int i = 0; i < p->count; i++) {
        live_agent_t *a = &p->agents[i];
        if (a->status == AGENT_STATUS_RUNNING && a->pid > 0) {
            kill(a->pid, SIGTERM);
            usleep(PROC_POLL_INTERVAL_US);
            kill(a->pid, SIGKILL);
            waitpid(a->pid, NULL, WNOHANG);
        }
        if (a->pipe_out >= 0)
            close(a->pipe_out);
        if (a->pipe_in >= 0)
            close(a->pipe_in);
        free(a->agent_id);
        free(a->task);
        free(a->agent_type);
        free(a->result);
    }
    memset(p, 0, sizeof(*p));
}

agent_pool_t *agent_pool_global(void) {
    if (!g_pool_initialized) {
        agent_pool_init(&g_pool);
        g_pool_initialized = 1;
    }
    return &g_pool;
}

static live_agent_t *find_agent(agent_pool_t *p, const char *id) {
    for (int i = 0; i < p->count; i++) {
        if (strcmp(p->agents[i].agent_id, id) == 0)
            return &p->agents[i];
    }
    return NULL;
}

static void read_nonblocking_pipe_into_sb(int fd, strbuf_t *sb, size_t max_bytes) {
    if (fd < 0 || !sb)
        return;
    nc_set_nonblock(fd);
    char buf[NC_IO_BUF];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (sb->len + (size_t)n > max_bytes)
            break;
        strbuf_append(sb, buf, n);
    }
}

static void check_agent_status(live_agent_t *a) {
    if (a->status != AGENT_STATUS_RUNNING && a->status != AGENT_STATUS_PAUSED)
        return;
    int wstatus;
    pid_t r = waitpid(a->pid, &wstatus, WNOHANG | WUNTRACED | WCONTINUED);
    if (r <= 0)
        return;
    if (WIFSTOPPED(wstatus)) {
        a->status = AGENT_STATUS_PAUSED;
        return;
    }
    if (WIFCONTINUED(wstatus)) {
        a->status = AGENT_STATUS_RUNNING;
        return;
    }
    if (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus))
        return;

    if (a->pipe_in >= 0) {
        close(a->pipe_in);
        a->pipe_in = -1;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    if (a->pipe_out >= 0) {
        read_nonblocking_pipe_into_sb(a->pipe_out, &sb, AGENT_OUTPUT_MAX);
        close(a->pipe_out);
        a->pipe_out = -1;
    }
    a->result = sb.data ? strbuf_detach(&sb) : nc_xstrdup("(no output)");
    strbuf_free(&sb);
    a->status = (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) ? AGENT_STATUS_COMPLETED
                                                                  : AGENT_STATUS_FAILED;
}

char *agent_pool_spawn(agent_pool_t *p, const char *task, const char *agent_type,
                       const void *cfg_ptr) {
    const config_t *cfg = (const config_t *)cfg_ptr;
    if (p->count >= MAX_LIVE_AGENTS)
        return NULL;

    live_agent_t *a = &p->agents[p->count];
    memset(a, 0, sizeof(*a));
    a->agent_id = gen_agent_id();
    a->task = nc_xstrdup(task);
    a->agent_type = agent_type ? nc_xstrdup(agent_type) : NULL;
    a->start_time = nc_wall_sec();
    a->pipe_out = -1;
    a->pipe_in = -1;

    const agent_def_t *adef = NULL;
    if (agent_type)
        adef = agent_def_find(agent_type);

    const char *use_model = NULL;
    if (adef && adef->model)
        use_model = adef->model;
    else if (cfg)
        use_model = config_model(cfg);

    int max_turns = (adef && adef->max_turns > 0) ? adef->max_turns : 30;

    int pipefd[2]; /* stdout: child writes, parent reads */
    int inpipe[2]; /* stdin:  parent writes, child reads */
    if (nc_pipe_cloexec(pipefd) < 0) {
        free(a->agent_id);
        free(a->task);
        free(a->agent_type);
        return NULL;
    }
    if (nc_pipe_cloexec(inpipe) < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        free(a->agent_id);
        free(a->task);
        free(a->agent_type);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        close(inpipe[0]);
        close(inpipe[1]);
        free(a->agent_id);
        free(a->task);
        free(a->agent_type);
        return NULL;
    }

    if (pid == 0) {
        nc_child_setup();
        close(pipefd[0]);
        close(inpipe[1]);
        dup2(inpipe[0], STDIN_FILENO);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(inpipe[0]);
        close(pipefd[1]);

        char *self = agent_get_self_path();
        char turns_str[NC_NUM_MAX];
        snprintf(turns_str, sizeof(turns_str), "%d", max_turns);
        const char *api_key = config_api_key(cfg);

        if (cfg && cfg->provider == PROVIDER_COMPAT) {
            const char *base = config_base_url(cfg);
            if (use_model && api_key)
                execl(self, NC_BINARY_NAME, "--provider", "compat", "--base-url", base, "-k",
                      api_key, "-m", use_model, "--max-turns", turns_str, "-p", task, "-D",
                      (char *)NULL);
            else if (use_model)
                execl(self, NC_BINARY_NAME, "--provider", "compat", "--base-url", base, "-m",
                      use_model, "--max-turns", turns_str, "-p", task, "-D", (char *)NULL);
            else
                execl(self, NC_BINARY_NAME, "--provider", "compat", "--base-url", base,
                      "--max-turns", turns_str, "-p", task, "-D", (char *)NULL);
        } else {
            if (use_model && api_key)
                execl(self, NC_BINARY_NAME, "-k", api_key, "-m", use_model, "--max-turns",
                      turns_str, "-p", task, "-D", (char *)NULL);
            else if (use_model)
                execl(self, NC_BINARY_NAME, "-m", use_model, "--max-turns", turns_str, "-p", task,
                      "-D", (char *)NULL);
            else
                execl(self, NC_BINARY_NAME, "--max-turns", turns_str, "-p", task, "-D",
                      (char *)NULL);
        }
        _exit(127);
    }

    close(pipefd[1]);
    close(inpipe[0]);
    a->pid = pid;
    a->pipe_out = pipefd[0];
    a->pipe_in = inpipe[1];
    a->status = AGENT_STATUS_RUNNING;
    p->count++;

    return nc_xstrdup(a->agent_id);
}

char *agent_pool_spawn_ex(agent_pool_t *p, const char *task, const char *agent_type,
                          const void *cfg_ptr, const agent_spawn_opts_t *opts) {
    if (!opts || opts->mode == FORK_NONE)
        return agent_pool_spawn(p, task, agent_type, cfg_ptr);

    strbuf_t sb;
    strbuf_init(&sb);

    if (opts->context_hint)
        strbuf_appendf(&sb, "[Context] %s\n\n", opts->context_hint);

    if (opts->mode == FORK_FULL) {
        strbuf_appendf(&sb,
                       "[Forked from parent — full history available as background context]\n\n"
                       "Task: %s",
                       task);
    } else {
        strbuf_appendf(&sb,
                       "[Forked from parent — last %d turns as context]\n\n"
                       "Task: %s",
                       opts->last_n > 0 ? opts->last_n : AGENT_FORK_LAST_N_DEFAULT, task);
    }

    char *enriched_task = strbuf_detach(&sb);
    char *result = agent_pool_spawn(p, enriched_task, agent_type, cfg_ptr);
    free(enriched_task);
    return result;
}

int agent_pool_list(agent_pool_t *p, char **output) {
    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < p->count; i++) {
        check_agent_status(&p->agents[i]);
    }

    if (p->count == 0) {
        *output = nc_xstrdup("No agents currently active.");
        return 0;
    }

    strbuf_appendf(&sb, "Active agents (%d):\n", p->count);

    for (int i = 0; i < p->count; i++) {
        live_agent_t *a = &p->agents[i];
        const char *status_str = "running";
        if (a->status == AGENT_STATUS_COMPLETED)
            status_str = "completed";
        else if (a->status == AGENT_STATUS_FAILED)
            status_str = "failed";
        else if (a->status == AGENT_STATUS_PAUSED)
            status_str = "paused";

        long elapsed = nc_wall_sec() - a->start_time;
        char task_preview[NC_PREVIEW_MAX];
        nc_trunc_preview(task_preview, a->task, sizeof(task_preview));

        strbuf_appendf(&sb, "  [%s] %s (%s, %lds) — %s\n", a->agent_id, status_str,
                       a->agent_type ? a->agent_type : "general", elapsed, task_preview);
    }

    *output = strbuf_detach(&sb);
    return 0;
}

int agent_pool_wait(agent_pool_t *p, const char *agent_id, char **output) {
    live_agent_t *a = find_agent(p, agent_id);
    if (!a) {
        char msg[NC_LABEL_MAX];
        snprintf(msg, sizeof(msg), "Agent '%s' not found.", agent_id);
        *output = nc_xstrdup(msg);
        return 1;
    }

    if (a->status != AGENT_STATUS_RUNNING) {
        *output = a->result ? nc_xstrdup(a->result) : nc_xstrdup("(no output)");
        return a->status == AGENT_STATUS_COMPLETED ? 0 : 1;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    int timeout_ms = AGENT_TIMEOUT_LONG_MS;

    struct pollfd pfd = {.fd = a->pipe_out, .events = POLLIN};
    while (1) {
        int r = poll(&pfd, 1, timeout_ms);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            kill(a->pid, SIGTERM);
            usleep(PROC_TERM_GRACE_US);
            kill(a->pid, SIGKILL);
            strbuf_append_str(&sb, "\n[agent timed out]");
            break;
        }
        char buf[NC_IO_BUF];
        ssize_t n = read(a->pipe_out, buf, sizeof(buf));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        if (sb.len + (size_t)n > AGENT_OUTPUT_MAX) {
            strbuf_append_str(&sb, "\n[output truncated]");
            break;
        }
        strbuf_append(&sb, buf, n);
    }

    close(a->pipe_out);
    a->pipe_out = -1;

    int wstatus;
    waitpid(a->pid, &wstatus, 0);

    a->result = sb.data ? strbuf_detach(&sb) : nc_xstrdup("(no output)");
    strbuf_free(&sb);
    a->status = (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) ? AGENT_STATUS_COMPLETED
                                                                  : AGENT_STATUS_FAILED;

    *output = nc_xstrdup(a->result);
    return a->status == AGENT_STATUS_COMPLETED ? 0 : 1;
}

int agent_pool_close(agent_pool_t *p, const char *agent_id, char **output) {
    live_agent_t *a = find_agent(p, agent_id);
    if (!a) {
        char msg[NC_LABEL_MAX];
        snprintf(msg, sizeof(msg), "Agent '%s' not found.", agent_id);
        *output = nc_xstrdup(msg);
        return 1;
    }

    if (a->pipe_in >= 0) {
        close(a->pipe_in);
        a->pipe_in = -1;
    }

    if (a->status == AGENT_STATUS_RUNNING && a->pid > 0) {
        kill(a->pid, SIGTERM);
        usleep(PROC_TERM_GRACE_US);
        int wstatus;
        pid_t r = waitpid(a->pid, &wstatus, WNOHANG);
        if (r == 0) {
            kill(a->pid, SIGKILL);
            waitpid(a->pid, &wstatus, 0);
        }
    }

    if (a->pipe_out >= 0) {
        strbuf_t sb;
        strbuf_init(&sb);
        nc_set_nonblock(a->pipe_out);
        char buf[NC_IO_BUF];
        ssize_t n;
        while ((n = read(a->pipe_out, buf, sizeof(buf))) > 0)
            strbuf_append(&sb, buf, n);
        close(a->pipe_out);
        a->pipe_out = -1;
        if (!a->result)
            a->result = sb.data ? strbuf_detach(&sb) : nc_xstrdup("(closed)");
        strbuf_free(&sb);
    }

    a->status = AGENT_STATUS_FAILED;
    char msg[NC_LABEL_MAX];
    snprintf(msg, sizeof(msg), "Agent '%s' closed.", agent_id);
    *output = nc_xstrdup(msg);
    return 0;
}

int agent_pool_count(const agent_pool_t *p) {
    return p->count;
}

int agent_pool_active_count(agent_pool_t *p) {
    int n = 0;
    for (int i = 0; i < p->count; i++) {
        check_agent_status(&p->agents[i]);
        if (p->agents[i].status == AGENT_STATUS_RUNNING)
            n++;
    }
    return n;
}

int agent_pool_partial_output(agent_pool_t *p, const char *agent_id, char **output, int *running) {
    if (!p || !agent_id || !output)
        return 1;

    live_agent_t *a = find_agent(p, agent_id);
    if (!a) {
        *output = nc_xstrdup("error: task not found");
        if (running)
            *running = 0;
        return 1;
    }

    check_agent_status(a);
    if (running)
        *running = (a->status == AGENT_STATUS_RUNNING || a->status == AGENT_STATUS_PAUSED) ? 1 : 0;

    strbuf_t sb;
    strbuf_init(&sb);
    if (a->result)
        strbuf_append_str(&sb, a->result);

    if ((a->status == AGENT_STATUS_RUNNING || a->status == AGENT_STATUS_PAUSED) &&
        a->pipe_out >= 0) {
        read_nonblocking_pipe_into_sb(a->pipe_out, &sb, AGENT_OUTPUT_MAX);
    }

    if (a->result)
        free(a->result);
    a->result = sb.data ? strbuf_detach(&sb) : nc_xstrdup("(no output yet)");
    strbuf_free(&sb);

    *output = nc_xstrdup(a->result ? a->result : "(no output yet)");
    return 0;
}

/* Tool handlers */

static const char *SPAWN_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"message\":{\"type\":\"string\",\"description\":\"Task description for the agent\"},"
    "\"agent_type\":{\"type\":\"string\",\"description\":"
    "\"Agent type: explore, plan, general, verification, or custom name\"},"
    "\"model\":{\"type\":\"string\",\"description\":\"Optional model override\"}"
    "},"
    "\"required\":[\"message\"]"
    "}";

static const char *LIST_SCHEMA = "{"
                                 "\"type\":\"object\","
                                 "\"properties\":{},"
                                 "\"additionalProperties\":false"
                                 "}";

static const char *WAIT_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"agent_id\":{\"type\":\"string\",\"description\":\"ID of the agent to wait for\"}"
    "},"
    "\"required\":[\"agent_id\"]"
    "}";

static const char *CLOSE_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"agent_id\":{\"type\":\"string\",\"description\":\"ID of the agent to close\"}"
    "},"
    "\"required\":[\"agent_id\"]"
    "}";

static int spawn_exec(const cJSON *input, char **output, void *ctx) {
    const char *msg = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)input, "message"));
    if (!msg) {
        *output = nc_xstrdup("error: missing 'message'");
        return 1;
    }

    const char *atype = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)input, "agent_type"));

    agent_pool_t *pool = agent_pool_global();
    const config_t *cfg = (const config_t *)ctx;
    char *id = agent_pool_spawn(pool, msg, atype, cfg);
    if (!id) {
        *output = nc_xstrdup("error: max agents reached or spawn failed");
        return 1;
    }

    char buf[NC_MSG_MAX];
    snprintf(buf, sizeof(buf),
             "Agent spawned: %s (type: %s). Use WaitAgent to get results, "
             "or ListAgents to check status.",
             id, atype ? atype : "general");
    *output = nc_xstrdup(buf);
    free(id);
    return 0;
}

static int list_exec(const cJSON *input, char **output, void *ctx) {
    (void)input;
    (void)ctx;
    return agent_pool_list(agent_pool_global(), output);
}

static int wait_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)input, "agent_id"));
    if (!id) {
        *output = nc_xstrdup("error: missing 'agent_id'");
        return 1;
    }
    return agent_pool_wait(agent_pool_global(), id, output);
}

static int close_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)input, "agent_id"));
    if (!id) {
        *output = nc_xstrdup("error: missing 'agent_id'");
        return 1;
    }
    return agent_pool_close(agent_pool_global(), id, output);
}

/* --- SendMessage: write to a running agent's stdin --- */

int agent_pool_send_message(agent_pool_t *p, const char *agent_id, const char *message) {
    if (!p || !agent_id || !message)
        return -1;
    for (int i = 0; i < p->count; i++) {
        live_agent_t *a = &p->agents[i];
        if (a->agent_id && strcmp(a->agent_id, agent_id) == 0) {
            if (a->status != AGENT_STATUS_RUNNING)
                return -2;
            if (a->pipe_in < 0)
                return -3;
            size_t len = strlen(message);
            ssize_t w = write(a->pipe_in, message, len);
            if (w < 0)
                return -3;
            if (message[len - 1] != '\n')
                write(a->pipe_in, "\n", 1);
            return 0;
        }
    }
    return -1;
}

static const char *SEND_MSG_SCHEMA =
    "{\"type\":\"object\",\"properties\":{"
    "\"agent_id\":{\"type\":\"string\",\"description\":\"ID of the target agent\"},"
    "\"message\":{\"type\":\"string\",\"description\":\"Message to send to the agent\"}"
    "},\"required\":[\"agent_id\",\"message\"]}";

static tool_def_t spawn_tool, list_tool, wait_tool, close_tool, send_msg_tool;

void tool_multi_agent_register(void) {
    spawn_tool.name = "SpawnAgent";
    spawn_tool.description = "Spawn a new background agent to work on a task autonomously. "
                             "Returns an agent_id for tracking. Use WaitAgent to collect results.";
    spawn_tool.input_schema = cJSON_Parse(SPAWN_SCHEMA);
    spawn_tool.execute = spawn_exec;
    spawn_tool.read_only = false;
    spawn_tool.concurrency_safe = false;
    spawn_tool.destructive = false;
    tools_register(&spawn_tool);

    list_tool.name = "ListAgents";
    list_tool.description =
        "List all spawned agents and their current status (running/completed/failed).";
    list_tool.input_schema = cJSON_Parse(LIST_SCHEMA);
    list_tool.execute = list_exec;
    list_tool.read_only = true;
    list_tool.concurrency_safe = true;
    list_tool.destructive = false;
    tools_register(&list_tool);

    wait_tool.name = "WaitAgent";
    wait_tool.description = "Wait for a specific agent to complete and return its output. "
                            "Blocks until the agent finishes or times out.";
    wait_tool.input_schema = cJSON_Parse(WAIT_SCHEMA);
    wait_tool.execute = wait_exec;
    wait_tool.read_only = true;
    wait_tool.concurrency_safe = false;
    wait_tool.destructive = false;
    tools_register(&wait_tool);

    close_tool.name = "CloseAgent";
    close_tool.description =
        "Terminate a running agent. Collects any partial output before closing.";
    close_tool.input_schema = cJSON_Parse(CLOSE_SCHEMA);
    close_tool.execute = close_exec;
    close_tool.read_only = false;
    close_tool.concurrency_safe = false;
    close_tool.destructive = false;
    tools_register(&close_tool);

    send_msg_tool.name = "SendMessage";
    send_msg_tool.description = "Send a message to a running agent. The message is queued "
                                "for delivery. Use ListAgents to check agent status first.";
    send_msg_tool.input_schema = cJSON_Parse(SEND_MSG_SCHEMA);
    send_msg_tool.execute = send_msg_exec;
    send_msg_tool.read_only = false;
    send_msg_tool.concurrency_safe = true;
    send_msg_tool.destructive = false;
    tools_register(&send_msg_tool);
}
