/*
 * collab_v2.c — Enhanced multi-agent collaboration protocol
 *
 * Manages a pool of collaborative tasks with depth limits,
 * spawn/follow-up/complete/cancel lifecycle, and JSON status export.
 */

#include "nevercode.h"
#include "defaults.h"

typedef enum {
    CTS_PENDING = 0,
    CTS_RUNNING,
    CTS_DONE,
    CTS_FAILED,
    CTS_CANCELLED
} collab_task_status_t;

typedef struct {
    char id[COLLAB_TASK_ID_MAX];
    char agent[NC_FMT_MAX];
    char desc[COLLAB_TASK_DESC_MAX];
    int fork_mode;
    collab_task_status_t state;
    long created_at;
    long finished_at;
    int depth;
    char *result;
    char *input;
} collab_task_t;

static struct {
    collab_task_t tasks[COLLAB_MAX_TASKS];
    int count;
    int active;
    int max_depth;
} g_collab;

void collab_v2_init(void) {
    memset(&g_collab, 0, sizeof(g_collab));
    g_collab.max_depth = COLLAB_MAX_DEPTH;
}

void collab_v2_free(void) {
    for (int i = 0; i < g_collab.count; i++) {
        free(g_collab.tasks[i].result);
        free(g_collab.tasks[i].input);
    }
    memset(&g_collab, 0, sizeof(g_collab));
}

static int gen_id(char *buf, size_t cap) {
    static int counter;
    snprintf(buf, cap, "task_%d_%ld", ++counter, nc_wall_sec());
    return 0;
}

int collab_v2_spawn(const char *agent_type, const char *msg, int fork_mode, int parent_depth,
                    char *out_id, size_t out_id_cap) {
    if (!msg || !out_id)
        return -1;

    int child_depth = parent_depth + 1;
    if (child_depth > g_collab.max_depth)
        return -2;
    if (g_collab.count >= COLLAB_MAX_TASKS)
        return -3;
    if (g_collab.active >= COLLAB_MAX_ACTIVE)
        return -4;

    collab_task_t *t = &g_collab.tasks[g_collab.count];
    memset(t, 0, sizeof(*t));
    gen_id(t->id, sizeof(t->id));
    nc_strlcpy(out_id, t->id, out_id_cap);
    nc_strlcpy(t->agent, agent_type ? agent_type : "worker", sizeof(t->agent));

    size_t msg_len = strlen(msg);
    size_t desc_len = msg_len < COLLAB_TASK_DESC_MAX - 1 ? msg_len : COLLAB_TASK_DESC_MAX - 1;
    memcpy(t->desc, msg, desc_len);
    t->desc[desc_len] = '\0';

    t->fork_mode = fork_mode;
    t->state = CTS_PENDING;
    t->created_at = nc_wall_sec();
    t->depth = child_depth;
    t->input = nc_xstrdup(msg);

    g_collab.count++;
    g_collab.active++;
    return 0;
}

int collab_v2_followup(const char *task_id, const char *msg, char *new_id, size_t new_id_cap) {
    if (!task_id || !msg)
        return -1;

    for (int i = 0; i < g_collab.count; i++) {
        if (strcmp(g_collab.tasks[i].id, task_id) == 0) {
            collab_task_t *p = &g_collab.tasks[i];
            return collab_v2_spawn(p->agent, msg, p->fork_mode, p->depth, new_id, new_id_cap);
        }
    }
    return -1;
}

int collab_v2_complete(const char *task_id, const char *summary) {
    if (!task_id)
        return -1;

    for (int i = 0; i < g_collab.count; i++) {
        if (strcmp(g_collab.tasks[i].id, task_id) == 0) {
            g_collab.tasks[i].state = CTS_DONE;
            g_collab.tasks[i].finished_at = nc_wall_sec();
            free(g_collab.tasks[i].result);
            g_collab.tasks[i].result = summary ? nc_xstrdup(summary) : NULL;
            if (g_collab.active > 0)
                g_collab.active--;
            return 0;
        }
    }
    return -1;
}

int collab_v2_active_count(void) {
    return g_collab.active;
}

char *collab_v2_list_json(void) {
    cJSON *arr = cJSON_CreateArray();
    static const char *status_names[] = {"pending", "running", "done", "failed", "cancelled"};

    for (int i = 0; i < g_collab.count; i++) {
        collab_task_t *t = &g_collab.tasks[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "id", t->id);
        cJSON_AddStringToObject(obj, "agent", t->agent);

        int si = (int)t->state;
        if (si < 0 || si > 4)
            si = 3;
        cJSON_AddStringToObject(obj, "status", status_names[si]);
        cJSON_AddNumberToObject(obj, "depth", t->depth);
        if (t->result)
            cJSON_AddStringToObject(obj, "result", t->result);
        cJSON_AddItemToArray(arr, obj);
    }

    return nc_cjson_detach(arr);
}
