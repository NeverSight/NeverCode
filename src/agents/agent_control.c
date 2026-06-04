/*
 * agent_control.c — pause / resume / cancel / restart for pool agents
 */

#include "nevercode.h"
#include "tools.h"

static live_agent_t *find_agent(agent_pool_t *p, const char *id) {
    if (!p || !id)
        return NULL;
    for (int i = 0; i < p->count; i++) {
        if (p->agents[i].agent_id && strcmp(p->agents[i].agent_id, id) == 0)
            return &p->agents[i];
    }
    return NULL;
}

int agent_control_pause(const char *agent_id) {
    agent_pool_t *p = agent_pool_global();
    live_agent_t *a = find_agent(p, agent_id);
    if (!a)
        return -1;
    if (a->status != AGENT_STATUS_RUNNING || a->pid <= 0)
        return -2;
    if (kill(a->pid, SIGSTOP) != 0)
        return -3;
    return 0;
}

int agent_control_resume(const char *agent_id) {
    agent_pool_t *p = agent_pool_global();
    live_agent_t *a = find_agent(p, agent_id);
    if (!a)
        return -1;
    if (a->pid <= 0)
        return -2;
    if (a->status == AGENT_STATUS_PAUSED) {
        if (kill(a->pid, SIGCONT) != 0)
            return -3;
        a->status = AGENT_STATUS_RUNNING;
        return 0;
    }
    if (a->status == AGENT_STATUS_RUNNING) {
        if (kill(a->pid, SIGCONT) != 0)
            return -3;
        return 0;
    }
    return -2;
}

int agent_control_cancel(const char *agent_id) {
    char *out = NULL;
    int rc = agent_pool_close(agent_pool_global(), agent_id, &out);
    free(out);
    return rc == 0 ? 0 : -1;
}

int agent_control_restart(const char *agent_id) {
    /* Same as cancel: stop the process; caller may SpawnAgent again with a new task. */
    return agent_control_cancel(agent_id);
}

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"agent_id\":{\"type\":\"string\",\"description\":\"Target agent id\"},"
    "\"action\":{\"type\":\"string\",\"description\":"
    "\"One of: pause, resume, cancel, restart\"}"
    "},"
    "\"required\":[\"agent_id\",\"action\"]"
    "}";

int agent_control_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    if (!input || !output)
        return -1;
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)input, "agent_id"));
    const char *act = cJSON_GetStringValue(cJSON_GetObjectItem((cJSON *)input, "action"));
    if (!id || !act) {
        *output = nc_xstrdup("Missing agent_id or action");
        return 1;
    }
    int rc;
    strbuf_t sb;
    strbuf_init(&sb);
    if (strcasecmp(act, "pause") == 0) {
        rc = agent_control_pause(id);
        if (rc == 0)
            strbuf_appendf(&sb, "Paused agent %s", id);
        else if (rc == -2)
            strbuf_appendf(&sb, "Agent %s is not running", id);
        else
            strbuf_appendf(&sb, "Could not pause agent %s", id);
    } else if (strcasecmp(act, "resume") == 0) {
        rc = agent_control_resume(id);
        if (rc == 0)
            strbuf_appendf(&sb, "Resumed agent %s", id);
        else
            strbuf_appendf(&sb, "Could not resume agent %s", id);
    } else if (strcasecmp(act, "cancel") == 0) {
        rc = agent_control_cancel(id);
        if (rc == 0)
            strbuf_appendf(&sb, "Cancelled / closed agent %s", id);
        else
            strbuf_appendf(&sb, "Could not cancel agent %s", id);
    } else if (strcasecmp(act, "restart") == 0) {
        rc = agent_control_restart(id);
        if (rc == 0)
            strbuf_appendf(&sb, "Stopped agent %s (spawn a new agent if needed)", id);
        else
            strbuf_appendf(&sb, "Could not restart agent %s", id);
    } else {
        *output = nc_xstrdup("Unknown action (use pause, resume, cancel, restart)");
        return 1;
    }
    *output = strbuf_detach(&sb);
    return (rc == 0) ? 0 : 1;
}

static tool_def_t agent_control_tool;

void tool_agent_control_register(void) {
    agent_control_tool.name = "AgentControl";
    agent_control_tool.description = "Pause, resume, cancel, or stop a spawned background agent "
                                     "(SIGSTOP/SIGCONT/TERM via pool).";
    agent_control_tool.input_schema = cJSON_Parse(SCHEMA);
    agent_control_tool.execute = agent_control_exec;
    agent_control_tool.read_only = false;
    agent_control_tool.concurrency_safe = false;
    agent_control_tool.destructive = true;
    tools_register(&agent_control_tool);
}
