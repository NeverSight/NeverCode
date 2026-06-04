#include "nevercode.h"
#include "config.h"
#include "tools.h"

static bool g_coordinator_mode = false;

static const char *COORDINATOR_PROMPT =
    "You are a Coordinator agent. Your role is to orchestrate complex tasks "
    "by delegating work to specialized Worker agents.\n\n"

    "# Core Rules\n\n"
    "1. You do NOT have file editing, bash, or read tools. You MUST delegate "
    "all actual work to Worker agents via the Agent tool.\n"
    "2. For simple questions that don't require code changes, answer directly.\n"
    "3. Break complex tasks into independent sub-tasks and assign each to a Worker.\n"
    "4. Workers run in parallel — design tasks to minimize dependencies.\n"
    "5. After all Workers complete, synthesize their results into a coherent response.\n\n"

    "# Available Tools\n\n"
    "- **Agent**: Spawn a Worker to perform a specific task. Give clear, "
    "self-contained instructions. Each Worker has full tool access.\n"
    "- **SendMessage**: Send follow-up instructions to a running Worker.\n"
    "- **TodoWrite**: Track task progress and sub-task status.\n\n"

    "# Task Decomposition Strategy\n\n"
    "1. Analyze the user's request and identify independent sub-tasks.\n"
    "2. For each sub-task, spawn a Worker with precise instructions:\n"
    "   - What files to modify or create\n"
    "   - What tests to run\n"
    "   - Expected deliverables\n"
    "3. Monitor Worker results. If a Worker fails, analyze the error and "
    "either retry with adjusted instructions or reassign.\n"
    "4. Never ask one Worker to check another Worker's output — do that yourself.\n"
    "5. Never predict or fabricate Worker results.\n\n"

    "# Anti-Patterns (AVOID)\n\n"
    "- Do NOT spawn a Worker just to read a file — you can't read it anyway.\n"
    "- Do NOT create long chains of Workers (A checks B's work).\n"
    "- Do NOT spawn more than 6 Workers simultaneously.\n"
    "- Do NOT give vague instructions. Each Worker needs self-contained context.\n";

static const char *coordinator_tool_whitelist[] = {"Agent", "SendMessage", "TodoWrite", NULL};

void coordinator_init(void) {
    const char *env = getenv(ENV_COORDINATOR_MODE);
    if (nc_parse_bool(env, false))
        g_coordinator_mode = true;
}

bool coordinator_active(void) {
    return g_coordinator_mode;
}

void coordinator_set_active(bool active) {
    g_coordinator_mode = active;
}

const char *coordinator_system_prompt(void) {
    return COORDINATOR_PROMPT;
}

bool coordinator_tool_allowed(const char *name) {
    if (!g_coordinator_mode)
        return true;
    for (int i = 0; coordinator_tool_whitelist[i]; i++) {
        if (strcasecmp(name, coordinator_tool_whitelist[i]) == 0)
            return true;
    }
    return false;
}

typedef struct {
    char *task_id;
    char *description;
    char *agent_id;
    int status; /* 0=pending 1=running 2=done 3=failed */
    char *result;
} coord_task_t;

/* MAX_COORD_TASKS in defaults.h */

static coord_task_t g_tasks[MAX_COORD_TASKS];
static int g_num_tasks = 0;

void coordinator_reset_tasks(void) {
    for (int i = 0; i < g_num_tasks; i++) {
        free(g_tasks[i].task_id);
        free(g_tasks[i].description);
        free(g_tasks[i].agent_id);
        free(g_tasks[i].result);
    }
    g_num_tasks = 0;
}

int coordinator_add_task(const char *description) {
    if (g_num_tasks >= MAX_COORD_TASKS)
        return -1;
    coord_task_t *t = &g_tasks[g_num_tasks];
    memset(t, 0, sizeof(*t));
    char buf[NC_SHORT_MAX];
    snprintf(buf, sizeof(buf), "task-%d", g_num_tasks + 1);
    t->task_id = nc_xstrdup(buf);
    t->description = nc_xstrdup(description);
    t->status = 0;
    return g_num_tasks++;
}

int coordinator_assign_task(int idx, const char *agent_id) {
    if (idx < 0 || idx >= g_num_tasks)
        return -1;
    free(g_tasks[idx].agent_id);
    g_tasks[idx].agent_id = nc_xstrdup(agent_id);
    g_tasks[idx].status = 1;
    return 0;
}

int coordinator_complete_task(int idx, const char *result, bool success) {
    if (idx < 0 || idx >= g_num_tasks)
        return -1;
    free(g_tasks[idx].result);
    g_tasks[idx].result = result ? nc_xstrdup(result) : NULL;
    g_tasks[idx].status = success ? 2 : 3;
    return 0;
}

int coordinator_find_task_by_agent(const char *agent_id) {
    for (int i = 0; i < g_num_tasks; i++) {
        if (g_tasks[i].agent_id && strcmp(g_tasks[i].agent_id, agent_id) == 0)
            return i;
    }
    return -1;
}

int coordinator_pending_count(void) {
    int c = 0;
    for (int i = 0; i < g_num_tasks; i++)
        if (g_tasks[i].status <= 1)
            c++;
    return c;
}

int coordinator_completed_count(void) {
    int c = 0;
    for (int i = 0; i < g_num_tasks; i++)
        if (g_tasks[i].status >= 2)
            c++;
    return c;
}

char *coordinator_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "# Coordinator Task Status\n\n");

    static const char *status_labels[] = {"pending", "running", "done", "failed"};
    static const char *status_icons[] = {"⏳", "🔄", "✅", "❌"};

    for (int i = 0; i < g_num_tasks; i++) {
        const coord_task_t *t = &g_tasks[i];
        strbuf_appendf(&sb, "%s **%s** [%s]: %s\n", status_icons[t->status], t->task_id,
                       status_labels[t->status], t->description);
        if (t->agent_id)
            strbuf_appendf(&sb, "   → agent: %s\n", t->agent_id);
        if (t->result && t->status >= 2)
            strbuf_appendf(&sb, "   → result: %.200s%s\n", t->result,
                           strlen(t->result) > 200 ? "..." : "");
    }

    int pending = coordinator_pending_count();
    int completed = coordinator_completed_count();
    strbuf_appendf(&sb, "\nProgress: %d/%d completed", completed, g_num_tasks);
    if (pending > 0)
        strbuf_appendf(&sb, " (%d pending)", pending);
    strbuf_append_str(&sb, "\n");

    return strbuf_detach(&sb);
}
