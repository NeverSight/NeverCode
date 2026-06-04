#include "nevercode.h"
#include "defaults.h"

typedef enum { P_IDLE = 0, P_PLAN, P_DISP, P_MON, P_MERGE, P_DONE } cm_p_t;

typedef struct {
    char desc[COORD_TASK_DESC_MAX];
    char aid[COORD_AGENT_ID_MAX];
    int pri, dep;
    bool started, done, failed;
    long st, et;
    char result[COORD_RESULT_MAX];
} cm_tk_t;

typedef struct {
    cm_p_t phase;
    char goal[NC_MSG_MAX];
    cm_tk_t tasks[COORD_MAX_TASKS];
    int tc, dc, fc;
    long ps;
    bool init;
    int tp;
} cm_ctx_t;

static cm_ctx_t gcm;

void coordinator_mode_init(void) {
    memset(&gcm, 0, sizeof(gcm));
    gcm.init = true;
}

void coordinator_mode_free(void) {
    memset(&gcm, 0, sizeof(gcm));
}

int coordinator_mode_plan(const char *gl) {
    if (!gcm.init || !gl)
        return -1;
    gcm.phase = P_PLAN;
    nc_strlcpy(gcm.goal, gl, sizeof(gcm.goal));
    gcm.tc = gcm.dc = gcm.fc = 0;
    gcm.ps = nc_wall_sec();
    gcm.tp++;
    return 0;
}

int coordinator_mode_add_task(const char *d, int pri, int dep) {
    if (!gcm.init || gcm.tc >= COORD_MAX_TASKS)
        return -1;
    cm_tk_t *t = &gcm.tasks[gcm.tc];
    memset(t, 0, sizeof(*t));
    nc_strlcpy(t->desc, d ? d : "", sizeof(t->desc));
    t->pri = pri;
    t->dep = dep;
    gcm.tc++;
    return gcm.tc - 1;
}

char *coordinator_mode_merge(void) {
    if (!gcm.init)
        return nc_xstrdup("(not init)");
    gcm.phase = P_MERGE;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Plan: %s\nResults (%d/%d done, %d failed):\n", gcm.goal, gcm.dc, gcm.tc,
                   gcm.fc);
    for (int i = 0; i < gcm.tc; i++) {
        cm_tk_t *t = &gcm.tasks[i];
        strbuf_appendf(&sb, "[%d] (%s) %s\n", i,
                       t->done ? "done" : (t->failed ? "FAIL" : "pending"), t->desc);
        if (t->result[0])
            strbuf_appendf(&sb, "    -> %.*s%s\n", COORD_RESULT_PREVIEW, t->result,
                           strlen(t->result) > COORD_RESULT_PREVIEW ? "..." : "");
    }
    gcm.phase = P_DONE;
    return strbuf_detach(&sb);
}

void coordinator_mode_reset(void) {
    gcm.phase = P_IDLE;
    gcm.tc = gcm.dc = gcm.fc = 0;
    gcm.goal[0] = '\0';
}

int coordinator_mode_task_count(void) {
    return gcm.tc;
}
int coordinator_mode_completed(void) {
    return gcm.dc;
}
int coordinator_mode_failed(void) {
    return gcm.fc;
}

int coordinator_mode_pending(void) {
    int c = 0;
    for (int i = 0; i < gcm.tc; i++)
        if (!gcm.tasks[i].done && !gcm.tasks[i].failed)
            c++;
    return c;
}

char *coordinator_mode_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *pn[] = {"idle", "planning", "dispatching", "monitoring", "merging", "complete"};
    const int pn_n = NC_ARRAY_LEN(pn);
    const char *phase = (gcm.phase >= 0 && gcm.phase < pn_n) ? pn[gcm.phase] : "unknown";
    strbuf_appendf(&sb,
                   "Coordinator: phase=%s goal='%s'\n"
                   "  tasks=%d done=%d failed=%d pending=%d plans=%d\n",
                   phase, gcm.goal, gcm.tc, gcm.dc, gcm.fc, coordinator_mode_pending(), gcm.tp);
    for (int i = 0; i < gcm.tc; i++) {
        cm_tk_t *t = &gcm.tasks[i];
        strbuf_appendf(&sb, "  [%d] %s p=%d dep=%d %.*s\n", i,
                       t->done      ? "done"
                       : t->failed  ? "FAIL"
                       : t->started ? "run"
                                    : "wait",
                       t->pri, t->dep, COORD_DESC_PREVIEW, t->desc);
    }
    return strbuf_detach(&sb);
}
