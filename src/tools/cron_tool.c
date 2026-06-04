/*
 * cron_tool.c — Background task scheduler
 */
#include "nevercode.h"
#include "tools.h"

/* Macros from defaults.h: SCHED_LABEL_MAX, NC_CMD_MAX */

typedef enum {
    SCHED_PENDING = 0,
    SCHED_RUNNING = 1,
    SCHED_COMPLETED = 2,
    SCHED_FAILED = 3,
    SCHED_CANCELLED = 4
} sched_status_t;

typedef struct {
    char id[NC_FMT_MAX];
    char label[SCHED_LABEL_MAX];
    char command[NC_CMD_MAX];
    int delay_s;
    int interval_s;
    int max_runs;
    int run_count;
    sched_status_t status;
    long created_at;
    long next_run_at;
    long last_run_at;
    char *last_output;
    int last_exit_code;
    pid_t pid;
    bool repeat;
} sched_task_t;

static sched_task_t g_tasks[MAX_SCHEDULED_TASKS];
static int g_task_count = 0;
static pthread_mutex_t g_sched_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_scheduler_running = false;
static pthread_t g_scheduler_thread;

static int gen_task_id(char *buf, size_t sz) {
    static int counter = 0;
    snprintf(buf, sz, "sched-%d-%ld", ++counter, nc_wall_sec() % 10000);
    return 0;
}

static void *scheduler_loop(void *arg) {
    (void)arg;
    while (g_scheduler_running) {
        long now = nc_wall_sec();
        pthread_mutex_lock(&g_sched_lock);
        for (int i = 0; i < g_task_count; i++) {
            sched_task_t *t = &g_tasks[i];
            if (t->status == SCHED_CANCELLED || t->status == SCHED_COMPLETED ||
                t->status == SCHED_RUNNING)
                continue;
            if (now < t->next_run_at)
                continue;
            t->status = SCHED_RUNNING;
            t->last_run_at = now;
            int pipefd[2];
            if (nc_pipe_cloexec(pipefd) < 0) {
                t->status = SCHED_FAILED;
                continue;
            }
            pid_t pid = fork();
            if (pid < 0) {
                close(pipefd[0]);
                close(pipefd[1]);
                t->status = SCHED_FAILED;
                continue;
            }
            if (pid == 0) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]);
                if (sandbox_enforcing() && os_sandbox_available())
                    os_sandbox_apply();
                execl("/bin/sh", "sh", "-c", t->command, NULL);
                _exit(127);
            }
            close(pipefd[1]);
            t->pid = pid;
            pthread_mutex_unlock(&g_sched_lock);
            char buf[NC_CMD_MAX];
            strbuf_t sb;
            strbuf_init(&sb);
            ssize_t n;
            while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                strbuf_append_str(&sb, buf);
            }
            close(pipefd[0]);
            int status;
            waitpid(pid, &status, 0);
            pthread_mutex_lock(&g_sched_lock);
            free(t->last_output);
            t->last_output = strbuf_detach(&sb);
            t->last_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            t->run_count++;
            t->pid = 0;
            if (t->repeat && (t->max_runs == 0 || t->run_count < t->max_runs)) {
                t->status = SCHED_PENDING;
                t->next_run_at = now + t->interval_s;
            } else {
                t->status = (t->last_exit_code == 0) ? SCHED_COMPLETED : SCHED_FAILED;
            }
        }
        pthread_mutex_unlock(&g_sched_lock);
        usleep(SCHED_POLL_INTERVAL_US);
    }
    return NULL;
}

void scheduler_init(void) {
    if (g_scheduler_running)
        return;
    g_scheduler_running = true;
    pthread_create(&g_scheduler_thread, NULL, scheduler_loop, NULL);
}

int scheduler_add(const char *label, const char *command, int delay_s, int interval_s, int max_runs,
                  char *id_out, size_t id_sz) {
    if (!command)
        return -1;
    pthread_mutex_lock(&g_sched_lock);
    if (g_task_count >= MAX_SCHEDULED_TASKS) {
        pthread_mutex_unlock(&g_sched_lock);
        return -1;
    }
    sched_task_t *t = &g_tasks[g_task_count];
    memset(t, 0, sizeof(*t));
    gen_task_id(t->id, sizeof(t->id));
    nc_strlcpy(t->label, label ? label : "task", sizeof(t->label));
    nc_strlcpy(t->command, command, sizeof(t->command));
    t->delay_s = delay_s;
    t->interval_s = interval_s > 0 ? interval_s : 0;
    t->max_runs = max_runs;
    t->repeat = interval_s > 0;
    t->status = SCHED_PENDING;
    t->created_at = nc_wall_sec();
    t->next_run_at = t->created_at + delay_s;
    if (id_out)
        nc_strlcpy(id_out, t->id, id_sz);
    g_task_count++;
    pthread_mutex_unlock(&g_sched_lock);
    if (!g_scheduler_running)
        scheduler_init();
    return 0;
}

int scheduler_cancel(const char *task_id) {
    if (!task_id)
        return -1;
    pthread_mutex_lock(&g_sched_lock);
    for (int i = 0; i < g_task_count; i++) {
        if (strcmp(g_tasks[i].id, task_id) == 0) {
            if (g_tasks[i].pid > 0)
                kill(g_tasks[i].pid, SIGTERM);
            g_tasks[i].status = SCHED_CANCELLED;
            pthread_mutex_unlock(&g_sched_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_sched_lock);
    return -1;
}
int scheduler_active_count(void) {
    int n = 0;
    pthread_mutex_lock(&g_sched_lock);
    for (int i = 0; i < g_task_count; i++)
        if (g_tasks[i].status == SCHED_PENDING || g_tasks[i].status == SCHED_RUNNING)
            n++;
    pthread_mutex_unlock(&g_sched_lock);
    return n;
}
const char *scheduler_task_output(const char *task_id) {
    if (!task_id)
        return NULL;
    for (int i = 0; i < g_task_count; i++)
        if (strcmp(g_tasks[i].id, task_id) == 0)
            return g_tasks[i].last_output;
    return NULL;
}

static const char *sched_status_name(sched_status_t s) {
    switch (s) {
    case SCHED_PENDING:
        return "pending";
    case SCHED_RUNNING:
        return "running";
    case SCHED_COMPLETED:
        return "completed";
    case SCHED_FAILED:
        return "failed";
    case SCHED_CANCELLED:
        return "cancelled";
    }
    return "unknown";
}

char *scheduler_format_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    pthread_mutex_lock(&g_sched_lock);
    strbuf_appendf(&sb, "Scheduled Tasks (%d):\n", g_task_count);
    for (int i = 0; i < g_task_count; i++) {
        sched_task_t *t = &g_tasks[i];
        strbuf_appendf(&sb, "  [%s] %s -- %s (runs: %d", t->id, t->label,
                       sched_status_name(t->status), t->run_count);
        if (t->repeat && t->interval_s > 0)
            strbuf_appendf(&sb, ", every %ds", t->interval_s);
        if (t->max_runs > 0)
            strbuf_appendf(&sb, ", max %d", t->max_runs);
        strbuf_append_str(&sb, ")\n");
    }
    pthread_mutex_unlock(&g_sched_lock);
    return strbuf_detach(&sb);
}

int cron_tool_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    if (!input) {
        *output = nc_xstrdup("Error: missing input");
        return 1;
    }
    const cJSON *action = cJSON_GetObjectItem(input, "action");
    const char *act = (action && cJSON_IsString(action)) ? action->valuestring : "schedule";
    if (strcmp(act, "schedule") == 0) {
        const cJSON *cmd = cJSON_GetObjectItem(input, "command");
        const cJSON *label = cJSON_GetObjectItem(input, "label");
        const cJSON *delay = cJSON_GetObjectItem(input, "delay_seconds");
        const cJSON *interval = cJSON_GetObjectItem(input, "interval_seconds");
        const cJSON *max = cJSON_GetObjectItem(input, "max_runs");
        if (!cmd || !cJSON_IsString(cmd)) {
            *output = nc_xstrdup("Error: 'command' is required");
            return 1;
        }
        char id[NC_FMT_MAX];
        int rc =
            scheduler_add(label && cJSON_IsString(label) ? label->valuestring : NULL,
                          cmd->valuestring, delay && cJSON_IsNumber(delay) ? delay->valueint : 0,
                          interval && cJSON_IsNumber(interval) ? interval->valueint : 0,
                          max && cJSON_IsNumber(max) ? max->valueint : 1, id, sizeof(id));
        if (rc < 0) {
            *output = nc_xstrdup("Error: scheduler full");
            return 1;
        }
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_appendf(&sb, "Scheduled task %s", id);
        *output = strbuf_detach(&sb);
        return 0;
    }
    if (strcmp(act, "cancel") == 0) {
        const cJSON *tid = cJSON_GetObjectItem(input, "task_id");
        if (!tid || !cJSON_IsString(tid)) {
            *output = nc_xstrdup("Error: 'task_id' required");
            return 1;
        }
        int rc = scheduler_cancel(tid->valuestring);
        *output = nc_xstrdup(rc == 0 ? "Cancelled" : "Error: task not found");
        return rc == 0 ? 0 : 1;
    }
    if (strcmp(act, "list") == 0) {
        *output = scheduler_format_list();
        return 0;
    }
    if (strcmp(act, "status") == 0) {
        const cJSON *tid = cJSON_GetObjectItem(input, "task_id");
        if (!tid || !cJSON_IsString(tid)) {
            *output = nc_xstrdup("Error: 'task_id' required");
            return 1;
        }
        const char *out = scheduler_task_output(tid->valuestring);
        *output = nc_xstrdup(out ? out : "(no output yet)");
        return 0;
    }
    *output = nc_xstrdup("Error: unknown action (schedule/cancel/list/status)");
    return 1;
}

#define CRON_SCHEMA                                                                                \
    "{\"type\":\"object\",\"properties\":{"                                                        \
    "\"action\":{\"type\":\"string\",\"enum\":[\"schedule\",\"cancel\",\"list\",\"status\"],"      \
    "\"description\":\"Action to perform\"},"                                                      \
    "\"command\":{\"type\":\"string\",\"description\":\"Shell command to execute\"},"              \
    "\"label\":{\"type\":\"string\",\"description\":\"Human-readable label\"},"                    \
    "\"delay_seconds\":{\"type\":\"integer\",\"description\":\"Seconds before first run\"},"       \
    "\"interval_seconds\":{\"type\":\"integer\",\"description\":\"Repeat interval in seconds (0 "  \
    "= one-shot)\"},"                                                                              \
    "\"max_runs\":{\"type\":\"integer\",\"description\":\"Max repetitions (0 = unlimited)\"},"     \
    "\"task_id\":{\"type\":\"string\",\"description\":\"Task ID for cancel/status\"}"              \
    "},\"required\":[\"action\"]}"

static tool_def_t cron_tool;

void tool_cron_register(void) {
    memset(&cron_tool, 0, sizeof(cron_tool));
    cron_tool.name = "Cron";
    cron_tool.description = "Schedule and manage background tasks. "
                            "Supports one-shot delayed execution and repeating intervals.";
    cron_tool.input_schema = cJSON_Parse(CRON_SCHEMA);
    cron_tool.execute = cron_tool_exec;
    cron_tool.read_only = false;
    cron_tool.concurrency_safe = true;
    cron_tool.destructive = false;
    tools_register(&cron_tool);
}
