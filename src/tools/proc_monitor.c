/*
 * proc_monitor.c — Background process monitoring tool.
 *
 * Provides a dashboard view of background processes spawned by the
 * agent (via Bash with background flag). Shows PID, status, runtime,
 * and tail of output for each tracked process.
 */

#include "nevercode.h"
#include "defaults.h"
#include "tools.h"

#define PMON_MAX_PROCS NC_PMON_MAX_PROCS
#define PMON_TAIL_LINES NC_PMON_TAIL_LINES
#define PMON_LABEL_MAX NC_PMON_LABEL_MAX

typedef enum {
    PMON_RUNNING,
    PMON_DONE,
    PMON_FAILED,
    PMON_KILLED,
} pmon_status_t;

typedef struct {
    pid_t pid;
    char label[PMON_LABEL_MAX];
    pmon_status_t status;
    int exit_code;
    time_t start_time;
    time_t end_time;
    bool active;
} pmon_entry_t;

static struct {
    pmon_entry_t entries[PMON_MAX_PROCS];
    int count;
} g_pmon;

void pmon_init(void) {
    memset(&g_pmon, 0, sizeof(g_pmon));
}

int pmon_track(pid_t pid, const char *label) {
    if (g_pmon.count >= PMON_MAX_PROCS) {
        int oldest = -1;
        for (int i = 0; i < PMON_MAX_PROCS; i++) {
            if (!g_pmon.entries[i].active) {
                oldest = i;
                break;
            }
        }
        if (oldest < 0)
            return -1;
        g_pmon.entries[oldest].active = false;
        pmon_entry_t *e = &g_pmon.entries[oldest];
        memset(e, 0, sizeof(*e));
        e->pid = pid;
        nc_strlcpy(e->label, label ? label : "", sizeof(e->label));
        e->status = PMON_RUNNING;
        e->start_time = nc_time_now();
        e->active = true;
        return oldest;
    }

    pmon_entry_t *e = &g_pmon.entries[g_pmon.count];
    e->pid = pid;
    nc_strlcpy(e->label, label ? label : "", sizeof(e->label));
    e->status = PMON_RUNNING;
    e->start_time = nc_time_now();
    e->active = true;
    return g_pmon.count++;
}

static void refresh_status(pmon_entry_t *e) {
    if (e->status != PMON_RUNNING)
        return;
    int status;
    pid_t r = waitpid(e->pid, &status, WNOHANG);
    if (r == 0)
        return;
    e->end_time = nc_time_now();
    if (r < 0) {
        e->status = PMON_FAILED;
        e->exit_code = -1;
    } else if (WIFEXITED(status)) {
        e->exit_code = WEXITSTATUS(status);
        e->status = (e->exit_code == 0) ? PMON_DONE : PMON_FAILED;
    } else if (WIFSIGNALED(status)) {
        e->status = PMON_KILLED;
        e->exit_code = WTERMSIG(status);
    }
}

static const char *status_label(pmon_status_t s) {
    switch (s) {
    case PMON_RUNNING:
        return "running";
    case PMON_DONE:
        return "done";
    case PMON_FAILED:
        return "failed";
    case PMON_KILLED:
        return "killed";
    }
    return "unknown";
}

int pmon_kill(pid_t pid) {
    for (int i = 0; i < g_pmon.count; i++) {
        if (g_pmon.entries[i].pid == pid && g_pmon.entries[i].active) {
            kill(pid, SIGTERM);
            g_pmon.entries[i].status = PMON_KILLED;
            g_pmon.entries[i].end_time = nc_time_now();
            return 0;
        }
    }
    return -1;
}

char *pmon_format_dashboard(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    int running = 0, done = 0, failed = 0;
    for (int i = 0; i < g_pmon.count; i++) {
        if (!g_pmon.entries[i].active)
            continue;
        refresh_status(&g_pmon.entries[i]);
        switch (g_pmon.entries[i].status) {
        case PMON_RUNNING:
            running++;
            break;
        case PMON_DONE:
            done++;
            break;
        case PMON_FAILED:
        case PMON_KILLED:
            failed++;
            break;
        }
    }

    strbuf_appendf(&sb, "Process Monitor: %d tracked | %d running | %d done | %d failed\n",
                   g_pmon.count, running, done, failed);
    strbuf_append_str(&sb, "─────────────────────────────────────────\n");

    for (int i = 0; i < g_pmon.count; i++) {
        pmon_entry_t *e = &g_pmon.entries[i];
        if (!e->active)
            continue;

        time_t elapsed = (e->end_time ? e->end_time : nc_time_now()) - e->start_time;
        strbuf_appendf(&sb, "  PID %-6d  %-8s  %llds  %s\n", e->pid, status_label(e->status),
                       (long long)elapsed, e->label);
    }

    if (g_pmon.count == 0)
        strbuf_append_str(&sb, "  (no processes tracked)\n");

    return strbuf_detach(&sb);
}

static int monitor_tool_execute(const cJSON *input, char **output, void *ctx) {
    (void)ctx;

    cJSON *jaction = cJSON_GetObjectItem(input, "action");
    const char *action = (jaction && cJSON_IsString(jaction)) ? jaction->valuestring : "list";

    if (strcmp(action, "kill") == 0) {
        cJSON *jpid = cJSON_GetObjectItem(input, "pid");
        if (!jpid || !cJSON_IsNumber(jpid)) {
            if (output)
                *output = nc_xstrdup("Error: pid required for kill action");
            return -1;
        }
        int rc = pmon_kill((pid_t)jpid->valueint);
        if (output) {
            if (rc == 0)
                *output = nc_xstrdup("Process terminated");
            else
                *output = nc_xstrdup("Process not found in monitor");
        }
        return rc;
    }

    char *dash = pmon_format_dashboard();
    if (output)
        *output = dash;
    else
        free(dash);
    return 0;
}

void proc_monitor_register_tool(void) {
    pmon_init();

    tool_def_t def = {0};
    def.name = "Monitor";
    def.description = "View and manage background processes. Lists all tracked processes "
                      "with PID, status, runtime, and label. Can also kill a process by PID.";
    def.execute = monitor_tool_execute;
    def.read_only = true;

    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *props = cJSON_AddObjectToObject(schema, "properties");
    cJSON *pa = cJSON_AddObjectToObject(props, "action");
    cJSON_AddStringToObject(pa, "type", "string");
    cJSON_AddStringToObject(pa, "description", "Action: 'list' (default) or 'kill'");
    cJSON *pp = cJSON_AddObjectToObject(props, "pid");
    cJSON_AddStringToObject(pp, "type", "integer");
    cJSON_AddStringToObject(pp, "description", "PID to kill (required for kill action)");
    def.input_schema = schema;
    tools_register(&def);
}

void tool_proc_monitor_register(void) {
    proc_monitor_register_tool();
}
