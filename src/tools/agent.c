#include "tools.h"
#include "config.h"
#include "nevercode.h"
#include <poll.h>

#define MAX_AGENT_OUTPUT AGENT_OUTPUT_MAX_BYTES
/* NC_AGENT_DEPTH_ENV in defaults.h */
#define DEPTH_ENV_KEY NC_AGENT_DEPTH_ENV

static int current_depth(void) {
    const char *v = getenv(DEPTH_ENV_KEY);
    return v ? nc_safe_atoi(v, 0) : 0;
}

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"prompt\":{\"type\":\"string\",\"description\":\"Task description for the sub-agent\"},"
    "\"agent_type\":{\"type\":\"string\",\"description\":"
    "\"Agent type: explore (read-only search), plan (structured planning), "
    "general (full access), verification (adversarial testing), or a custom agent name\"},"
    "\"model\":{\"type\":\"string\",\"description\":\"Optional model override\"},"
    "\"max_turns\":{\"type\":\"integer\",\"description\":\"Maximum conversation turns "
    "(default: " NC_STRINGIFY(NC_AGENT_DEFAULT_TURNS) ")\"}"
                                                      "},"
                                                      "\"required\":[\"prompt\"]"
                                                      "}";

static void build_agent_prompt(strbuf_t *sb, const char *prompt, const agent_def_t *adef) {
    if (adef && adef->name && adef->name[0]) {
        (void)agent_memory_ensure(adef->name, AGENT_MEM_PROJECT);
        char *mem = agent_memory_prompt(adef->name, AGENT_MEM_PROJECT, 0);
        if (mem) {
            strbuf_append_str(sb, mem);
            strbuf_append_str(sb, "\n");
            free(mem);
        }
    }
    if (adef && adef->system_prompt) {
        strbuf_append_str(sb, "<agent-instructions>\n");
        strbuf_append_str(sb, adef->system_prompt);
        strbuf_append_str(sb, "\n</agent-instructions>\n\n");
    }

    strbuf_append_str(sb, "<task>\n");
    strbuf_append_str(sb, prompt);
    strbuf_append_str(sb, "\n</task>\n\n");
    strbuf_append_str(sb, "You are a sub-agent. Complete the task and report your findings. "
                          "Do NOT ask questions or suggest next steps — just execute.");
}

static int agent_exec(const cJSON *input, char **output, void *ctx) {
    const char *prompt = nc_cjson_str((cJSON *)input, "prompt", NULL);
    if (!prompt) {
        *output = nc_xstrdup("error: missing 'prompt'");
        return 1;
    }

    int depth = current_depth();
    if (depth >= NC_MAX_DELEGATION_DEPTH) {
        *output =
            nc_xstrdup("error: delegation depth limit reached — cannot spawn deeper sub-agents");
        return 1;
    }

    const char *agent_type = nc_cjson_str((cJSON *)input, "agent_type", NULL);
    const char *model_override = nc_cjson_str((cJSON *)input, "model", NULL);
    cJSON *max_turns_j = cJSON_GetObjectItem((cJSON *)input, "max_turns");
    int max_turns =
        (max_turns_j && cJSON_IsNumber(max_turns_j)) ? (int)max_turns_j->valuedouble : 0;

    const config_t *cfg = (const config_t *)ctx;

    const agent_def_t *adef = NULL;
    if (agent_type) {
        adef = agent_def_find(agent_type);
        if (!adef) {
            char msg[NC_MSG_MAX];
            snprintf(msg, sizeof(msg),
                     "Unknown agent type '%s'. "
                     "Available: explore, plan, general, verification",
                     agent_type);
            *output = nc_xstrdup(msg);
            return 1;
        }
        if (!model_override && adef->model)
            model_override = adef->model;
        if (max_turns <= 0)
            max_turns = adef->max_turns;
    }
    if (max_turns <= 0)
        max_turns = NC_AGENT_DEFAULT_TURNS;

    strbuf_t full_prompt;
    strbuf_init(&full_prompt);
    build_agent_prompt(&full_prompt, prompt, adef);

    int pipefd[2];
    if (nc_pipe_cloexec(pipefd) < 0) {
        strbuf_free(&full_prompt);
        *output = nc_xstrdup("error: pipe failed");
        return 1;
    }

    rollout_record_fork(NULL, rollout_current_turn());

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        strbuf_free(&full_prompt);
        *output = nc_xstrdup("error: fork failed");
        return 1;
    }

    if (pid == 0) {
        nc_child_setup();
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        char depth_str[NC_NUM_MAX];
        snprintf(depth_str, sizeof(depth_str), "%d", depth + 1);
        setenv(DEPTH_ENV_KEY, depth_str, 1);

        char *self = agent_get_self_path();
        const char *use_model = model_override ? model_override : config_model(cfg);

        char turns_str[NC_NUM_MAX];
        snprintf(turns_str, sizeof(turns_str), "%d", max_turns);

        const char *prompt_str = full_prompt.data ? full_prompt.data : prompt;
        const char *api_key = cfg ? config_api_key(cfg) : NULL;

        if (cfg && cfg->provider == PROVIDER_COMPAT) {
            const char *base = config_base_url(cfg);
            if (use_model && api_key)
                execl(self, NC_BINARY_NAME, "--provider", "compat", "--base-url", base, "-k",
                      api_key, "-m", use_model, "--max-turns", turns_str, "-p", prompt_str, "-D",
                      (char *)NULL);
            else if (use_model)
                execl(self, NC_BINARY_NAME, "--provider", "compat", "--base-url", base, "-m",
                      use_model, "--max-turns", turns_str, "-p", prompt_str, "-D", (char *)NULL);
            else
                execl(self, NC_BINARY_NAME, "--provider", "compat", "--base-url", base,
                      "--max-turns", turns_str, "-p", prompt_str, "-D", (char *)NULL);
        } else {
            if (use_model && api_key)
                execl(self, NC_BINARY_NAME, "-k", api_key, "-m", use_model, "--max-turns",
                      turns_str, "-p", prompt_str, "-D", (char *)NULL);
            else if (use_model)
                execl(self, NC_BINARY_NAME, "-m", use_model, "--max-turns", turns_str, "-p",
                      prompt_str, "-D", (char *)NULL);
            else
                execl(self, NC_BINARY_NAME, "--max-turns", turns_str, "-p", prompt_str, "-D",
                      (char *)NULL);
        }
        _exit(127);
    }

    close(pipefd[1]);
    strbuf_free(&full_prompt);

    strbuf_t sb;
    strbuf_init(&sb);
    struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN};

    int timeout_ms = (adef && adef->max_turns > AGENT_LONG_TURN_THRESHOLD) ? AGENT_TIMEOUT_LONG_MS
                                                                           : AGENT_TIMEOUT_SHORT_MS;

    while (1) {
        int r = poll(&pfd, 1, timeout_ms);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            killpg(pid, SIGTERM);
            usleep(PROC_TERM_GRACE_US);
            killpg(pid, SIGKILL);
            strbuf_append_str(&sb, "\n[agent timed out]");
            break;
        }
        char buf[NC_IO_BUF];
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        if (sb.len + (size_t)n > MAX_AGENT_OUTPUT) {
            strbuf_append_str(&sb, "\n[output truncated]");
            break;
        }
        strbuf_append(&sb, buf, n);
    }

    close(pipefd[0]);
    int status;
    waitpid(pid, &status, 0);

    *output = sb.data ? strbuf_detach(&sb) : nc_xstrdup("(no output)");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static tool_def_t agent_tool;

void tool_agent_register(void) {
    agent_tool.name = "Agent";
    agent_tool.description =
        "Launch a sub-agent to handle a complex task autonomously. "
        "Supports agent types: explore (read-only search), plan (structured planning), "
        "general (full tool access), verification (adversarial testing). "
        "Use agent_type to select, or omit for general-purpose.";
    agent_tool.input_schema = cJSON_Parse(SCHEMA);
    agent_tool.execute = agent_exec;
    agent_tool.read_only = false;
    agent_tool.concurrency_safe = false;
    agent_tool.destructive = false;
    tools_register(&agent_tool);
}
