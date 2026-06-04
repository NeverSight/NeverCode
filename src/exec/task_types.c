#include "nevercode.h"

typedef struct {
    task_kind_t kind;
    char *label, *session_id, *detail;
    long created_at, started_at, finished_at;
    int exit_code;
    bool aborted;
} task_entry_t;

static struct {
    task_entry_t tasks[TASK_TYPE_MAX_ENTRIES];
    int count;
    bool initialized;
} g_tasks;

static task_entry_t *tfind(int id) {
    return (id >= 0 && id < g_tasks.count) ? &g_tasks.tasks[id] : NULL;
}

void task_types_init(void) {
    memset(&g_tasks, 0, sizeof(g_tasks));
    g_tasks.initialized = true;
}

void task_types_free(void) {
    for (int i = 0; i < g_tasks.count; i++) {
        free(g_tasks.tasks[i].label);
        free(g_tasks.tasks[i].session_id);
        free(g_tasks.tasks[i].detail);
    }
    memset(&g_tasks, 0, sizeof(g_tasks));
}

int task_types_create(task_kind_t kind, const char *label, const char *session_id) {
    if (!g_tasks.initialized || g_tasks.count >= TASK_TYPE_MAX_ENTRIES)
        return -1;
    int id = g_tasks.count;
    task_entry_t *t = &g_tasks.tasks[id];
    t->kind = kind;
    t->label = label ? nc_xstrdup(label) : nc_xstrdup("");
    t->session_id = session_id ? nc_xstrdup(session_id) : nc_xstrdup("");
    t->detail = NULL;
    t->created_at = nc_mono_ms();
    t->started_at = t->finished_at = 0;
    t->exit_code = -1;
    t->aborted = false;
    g_tasks.count++;
    return id;
}

int task_types_start(int id) {
    task_entry_t *t = tfind(id);
    if (!t)
        return -1;
    t->started_at = nc_mono_ms();
    return 0;
}

int task_types_finish(int id, int ec, const char *detail) {
    task_entry_t *t = tfind(id);
    if (!t)
        return -1;
    t->finished_at = nc_mono_ms();
    t->exit_code = ec;
    if (detail) {
        free(t->detail);
        t->detail = nc_xstrdup(detail);
    }
    return 0;
}

int task_types_abort(int id) {
    task_entry_t *t = tfind(id);
    if (!t)
        return -1;
    t->aborted = true;
    t->finished_at = nc_mono_ms();
    t->exit_code = -2;
    return 0;
}

bool task_types_running(int id) {
    task_entry_t *t = tfind(id);
    return t && t->started_at > 0 && t->finished_at == 0 && !t->aborted;
}

bool task_types_done(int id) {
    task_entry_t *t = tfind(id);
    return t && (t->finished_at > 0 || t->aborted);
}

long task_types_elapsed_ms(int id) {
    task_entry_t *t = tfind(id);
    if (!t || t->started_at == 0)
        return 0;
    long end = t->finished_at > 0 ? t->finished_at : nc_mono_ms();
    return end - t->started_at;
}

int task_types_count(void) {
    return g_tasks.count;
}

int task_types_active_count(void) {
    int n = 0;
    for (int i = 0; i < g_tasks.count; i++)
        if (task_types_running(i))
            n++;
    return n;
}

int task_types_by_kind(task_kind_t kind, int *ids, int max) {
    int n = 0;
    for (int i = 0; i < g_tasks.count && n < max; i++)
        if (g_tasks.tasks[i].kind == kind)
            ids[n++] = i;
    return n;
}

const char *task_kind_name(task_kind_t kind) {
    switch (kind) {
    case TASK_KIND_REGULAR:
        return "regular";
    case TASK_KIND_COMPACT:
        return "compact";
    case TASK_KIND_REVIEW:
        return "review";
    case TASK_KIND_UNDO:
        return "undo";
    case TASK_KIND_USER_SHELL:
        return "user_shell";
    case TASK_KIND_GHOST_SNAPSHOT:
        return "ghost_snapshot";
    case TASK_KIND_MEMORY:
        return "memory";
    case TASK_KIND_AGENT:
        return "agent";
    default:
        return "unknown";
    }
}

char *task_types_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Tasks: %d total, %d active\n", g_tasks.count, task_types_active_count());
    for (int i = 0; i < g_tasks.count; i++) {
        task_entry_t *t = &g_tasks.tasks[i];
        const char *st = t->aborted           ? "aborted"
                         : t->finished_at > 0 ? "done"
                         : t->started_at > 0  ? "running"
                                              : "pending";
        strbuf_appendf(&sb, "  [%d] %s (%s) -- %s", i, task_kind_name(t->kind), st, t->label);
        if (t->started_at > 0)
            strbuf_appendf(&sb, " [%ldms]", task_types_elapsed_ms(i));
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}
