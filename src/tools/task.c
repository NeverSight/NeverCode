#include "tools.h"
#include "config.h"
#include "nevercode.h"

static const char *TASK_CREATE_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"description\":{\"type\":\"string\",\"description\":\"Short 3-5 word description of the "
    "task\"},"
    "\"prompt\":{\"type\":\"string\",\"description\":\"Detailed task instructions for the "
    "sub-agent\"},"
    "\"agent_type\":{\"type\":\"string\",\"description\":"
    "\"Agent type: explore, plan, general, verification, or custom agent name\"},"
    "\"model\":{\"type\":\"string\",\"description\":\"Optional model override\"},"
    "\"run_in_background\":{\"type\":\"boolean\",\"description\":\"Run asynchronously (default: "
    "false)\"}"
    "},"
    "\"required\":[\"description\",\"prompt\"]"
    "}";

static const char *TASK_GET_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"task_id\":{\"type\":\"string\",\"description\":\"ID of the task to check\"}"
    "},"
    "\"required\":[\"task_id\"]"
    "}";

static const char *TASK_LIST_SCHEMA = "{"
                                      "\"type\":\"object\","
                                      "\"properties\":{}"
                                      "}";

static const char *TASK_STOP_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"task_id\":{\"type\":\"string\",\"description\":\"ID of the task to stop\"}"
    "},"
    "\"required\":[\"task_id\"]"
    "}";

static const char *TASK_UPDATE_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"task_id\":{\"type\":\"string\",\"description\":\"ID of the task to update\"},"
    "\"message\":{\"type\":\"string\",\"description\":\"Follow-up message to send\"}"
    "},"
    "\"required\":[\"task_id\",\"message\"]"
    "}";

static const char *TASK_OUTPUT_SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"task_id\":{\"type\":\"string\",\"description\":\"ID of the task\"},"
    "\"tail_lines\":{\"type\":\"integer\",\"description\":\"Number of tail lines (default: 50)\"}"
    "},"
    "\"required\":[\"task_id\"]"
    "}";

static int task_create_exec(const cJSON *input, char **output, void *ctx) {
    const char *desc = nc_cjson_str((cJSON *)input, "description", NULL);
    const char *prompt = nc_cjson_str((cJSON *)input, "prompt", NULL);
    if (!prompt || !desc) {
        *output = nc_xstrdup("error: missing 'prompt' or 'description'");
        return 1;
    }

    const char *agent_type = nc_cjson_str((cJSON *)input, "agent_type", NULL);
    if (!agent_type)
        agent_type = "general";

    agent_pool_t *pool = agent_pool_global();
    char *task_id = agent_pool_spawn(pool, prompt, agent_type, ctx);
    if (!task_id) {
        *output = nc_xstrdup("error: failed to spawn task (pool may be full)");
        return 1;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "task_id", task_id);
    cJSON_AddStringToObject(result, "status", "running");
    cJSON_AddStringToObject(result, "description", desc);
    cJSON_AddStringToObject(result, "agent_type", agent_type);

    cJSON *bg_j = cJSON_GetObjectItem((cJSON *)input, "run_in_background");
    bool bg = bg_j && cJSON_IsTrue(bg_j);

    if (!bg) {
        char *wait_output = NULL;
        agent_pool_wait(pool, task_id, &wait_output);
        if (wait_output) {
            cJSON_AddStringToObject(result, "status", "completed");
            cJSON_AddStringToObject(result, "result", wait_output);
            free(wait_output);
        } else {
            cJSON_AddStringToObject(result, "status", "completed");
            cJSON_AddStringToObject(result, "result", "(no output)");
        }
    }

    *output = nc_cjson_detach_pretty(result);
    free(task_id);
    return 0;
}

static int task_get_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *task_id = nc_cjson_str((cJSON *)input, "task_id", NULL);
    if (!task_id) {
        *output = nc_xstrdup("error: missing 'task_id'");
        return 1;
    }

    agent_pool_t *pool = agent_pool_global();
    char *result = NULL;
    int rc = agent_pool_wait(pool, task_id, &result);

    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "task_id", task_id);
    if (rc == 0 && result) {
        cJSON_AddStringToObject(out, "status", "completed");
        cJSON_AddStringToObject(out, "result", result);
    } else if (rc != 0) {
        cJSON_AddStringToObject(out, "status", "not_found");
        if (result)
            cJSON_AddStringToObject(out, "message", result);
    } else {
        cJSON_AddStringToObject(out, "status", "running");
    }
    free(result);
    *output = nc_cjson_detach_pretty(out);
    return 0;
}

static int task_list_exec(const cJSON *input, char **output, void *ctx) {
    (void)input;
    (void)ctx;
    agent_pool_t *pool = agent_pool_global();
    int rc = agent_pool_list(pool, output);
    if (rc < 0 || !*output)
        *output = nc_xstrdup("No tasks running.");
    return 0;
}

static int task_stop_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *task_id = nc_cjson_str((cJSON *)input, "task_id", NULL);
    if (!task_id) {
        *output = nc_xstrdup("error: missing 'task_id'");
        return 1;
    }

    agent_pool_t *pool = agent_pool_global();
    int rc = agent_pool_close(pool, task_id, output);
    if (rc != 0) {
        char msg[NC_LABEL_MAX];
        snprintf(msg, sizeof(msg), "Task '%s' not found or already stopped.", task_id);
        *output = nc_xstrdup(msg);
        return 1;
    }
    return 0;
}

static int task_update_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *task_id = nc_cjson_str((cJSON *)input, "task_id", NULL);
    const char *message = nc_cjson_str((cJSON *)input, "message", NULL);
    if (!task_id || !message) {
        *output = nc_xstrdup("error: missing 'task_id' or 'message'");
        return 1;
    }

    agent_pool_t *pool = agent_pool_global();
    int rc = agent_pool_send_message(pool, task_id, message);
    if (rc == 0) {
        cJSON *out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "task_id", task_id);
        cJSON_AddStringToObject(out, "status", "message_sent");
        *output = nc_cjson_detach_pretty(out);
    } else if (rc == -2) {
        *output = nc_xstrdup("error: task is not running");
    } else {
        *output = nc_xstrdup("error: task not found");
    }
    return rc == 0 ? 0 : 1;
}

static int task_output_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *task_id = nc_cjson_str((cJSON *)input, "task_id", NULL);
    if (!task_id) {
        *output = nc_xstrdup("error: missing 'task_id'");
        return 1;
    }

    cJSON *tail_j = cJSON_GetObjectItem((cJSON *)input, "tail_lines");
    int tail_lines =
        (tail_j && cJSON_IsNumber(tail_j)) ? (int)tail_j->valuedouble : TASK_DEFAULT_TAIL_LINES;
    if (tail_lines < 1)
        tail_lines = TASK_DEFAULT_TAIL_LINES;

    agent_pool_t *pool = agent_pool_global();
    char *raw_result = NULL;
    int running = 0;
    if (agent_pool_partial_output(pool, task_id, &raw_result, &running) != 0 || !raw_result) {
        *output = nc_xstrdup("error: task not found");
        free(raw_result);
        return 1;
    }

    /* Tail the output to requested number of lines */
    if (tail_lines > 0 && raw_result) {
        int total_lines = 1;
        for (const char *p = raw_result; *p; p++)
            if (*p == '\n')
                total_lines++;

        if (total_lines > tail_lines) {
            int skip = total_lines - tail_lines;
            const char *p = raw_result;
            for (int i = 0; i < skip && *p; p++)
                if (*p == '\n')
                    i++;
            char *trimmed = nc_xstrdup(p);
            free(raw_result);
            raw_result = trimmed;
        }
    }

    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "task_id", task_id);
    cJSON_AddStringToObject(out, "status", running ? "running" : "completed");
    cJSON_AddStringToObject(out, "output", raw_result);
    *output = nc_cjson_detach_pretty(out);
    free(raw_result);
    return 0;
}

static tool_def_t task_create_tool;
static tool_def_t task_get_tool;
static tool_def_t task_list_tool;
static tool_def_t task_stop_tool;
static tool_def_t task_update_tool;
static tool_def_t task_output_tool;

void tool_task_register(void) {
    task_create_tool.name = "TaskCreate";
    task_create_tool.description =
        "Launch a sub-agent task to handle complex work autonomously. "
        "Each task runs as an independent process with its own conversation. "
        "Use run_in_background=true for async tasks you check later.";
    task_create_tool.input_schema = cJSON_Parse(TASK_CREATE_SCHEMA);
    task_create_tool.execute = task_create_exec;
    task_create_tool.read_only = false;
    task_create_tool.concurrency_safe = false;
    tools_register(&task_create_tool);

    task_get_tool.name = "TaskGet";
    task_get_tool.description = "Check the status and result of a previously created task.";
    task_get_tool.input_schema = cJSON_Parse(TASK_GET_SCHEMA);
    task_get_tool.execute = task_get_exec;
    task_get_tool.read_only = true;
    task_get_tool.concurrency_safe = true;
    tools_register(&task_get_tool);

    task_list_tool.name = "TaskList";
    task_list_tool.description = "List all running and completed tasks.";
    task_list_tool.input_schema = cJSON_Parse(TASK_LIST_SCHEMA);
    task_list_tool.execute = task_list_exec;
    task_list_tool.read_only = true;
    task_list_tool.concurrency_safe = true;
    tools_register(&task_list_tool);

    task_stop_tool.name = "TaskStop";
    task_stop_tool.description = "Stop a running task by its task_id.";
    task_stop_tool.input_schema = cJSON_Parse(TASK_STOP_SCHEMA);
    task_stop_tool.execute = task_stop_exec;
    task_stop_tool.read_only = false;
    task_stop_tool.concurrency_safe = false;
    tools_register(&task_stop_tool);

    task_update_tool.name = "TaskUpdate";
    task_update_tool.description = "Send a follow-up message to a running task. "
                                   "The message is delivered to the task's stdin.";
    task_update_tool.input_schema = cJSON_Parse(TASK_UPDATE_SCHEMA);
    task_update_tool.execute = task_update_exec;
    task_update_tool.read_only = false;
    task_update_tool.concurrency_safe = true;
    tools_register(&task_update_tool);

    task_output_tool.name = "TaskOutput";
    task_output_tool.description =
        "Get the output of a task. For completed tasks returns the full result; "
        "for running tasks returns partial status. Use tail_lines to limit output.";
    task_output_tool.input_schema = cJSON_Parse(TASK_OUTPUT_SCHEMA);
    task_output_tool.execute = task_output_exec;
    task_output_tool.read_only = true;
    task_output_tool.concurrency_safe = true;
    tools_register(&task_output_tool);
}
