#include "nevercode.h"

static thread_mgr_t g_tmgr;
static bool g_tmgr_init_done = false;
static int g_thread_seq = 0;

static char *gen_thread_id(void) {
    char buf[NC_FMT_MAX];
    snprintf(buf, sizeof(buf), "thr_%lx_%04x_%02x", nc_wall_sec() & 0xFFFFF,
             (unsigned)(getpid() & 0xFFFF), (unsigned)(g_thread_seq++ & 0xFF));
    return nc_xstrdup(buf);
}

void thread_mgr_init(thread_mgr_t *t) {
    memset(t, 0, sizeof(*t));
}

void thread_mgr_free(thread_mgr_t *t) {
    for (int i = 0; i < t->count; i++) {
        free(t->threads[i].id);
        free(t->threads[i].label);
        free(t->threads[i].parent_id);
        free(t->threads[i].model);
        free(t->threads[i].last_task);
    }
    t->count = 0;
    t->active_idx = -1;
}

thread_mgr_t *thread_mgr_global(void) {
    if (!g_tmgr_init_done) {
        thread_mgr_init(&g_tmgr);
        g_tmgr_init_done = true;
    }
    return &g_tmgr;
}

int thread_mgr_create(thread_mgr_t *t, const char *label, const char *parent_id,
                      const char *model) {
    if (!t || t->count >= MAX_THREADS)
        return -1;
    thread_entry_t *e = &t->threads[t->count];
    memset(e, 0, sizeof(*e));
    e->id = gen_thread_id();
    e->label = label ? nc_xstrdup(label) : nc_xstrdup("main");
    e->parent_id = parent_id ? nc_xstrdup(parent_id) : NULL;
    e->model = model ? nc_xstrdup(model) : NULL;
    e->state = THREAD_ACTIVE;
    e->created_at = nc_wall_sec();
    int idx = t->count;
    t->count++;
    t->active_idx = idx;
    return idx;
}

int thread_mgr_find(const thread_mgr_t *t, const char *id) {
    if (!t || !id)
        return -1;
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->threads[i].id, id) == 0)
            return i;
    return -1;
}

int thread_mgr_suspend(thread_mgr_t *t, int idx) {
    if (!t || idx < 0 || idx >= t->count)
        return -1;
    if (t->threads[idx].state != THREAD_ACTIVE)
        return -1;
    t->threads[idx].state = THREAD_SUSPENDED;
    if (t->active_idx == idx)
        t->active_idx = -1;
    return 0;
}

int thread_mgr_resume(thread_mgr_t *t, int idx) {
    if (!t || idx < 0 || idx >= t->count)
        return -1;
    if (t->threads[idx].state != THREAD_SUSPENDED)
        return -1;
    t->threads[idx].state = THREAD_ACTIVE;
    t->active_idx = idx;
    return 0;
}

int thread_mgr_complete(thread_mgr_t *t, int idx) {
    if (!t || idx < 0 || idx >= t->count)
        return -1;
    t->threads[idx].state = THREAD_COMPLETED;
    if (t->active_idx == idx)
        t->active_idx = -1;
    return 0;
}

int thread_mgr_abort(thread_mgr_t *t, int idx) {
    if (!t || idx < 0 || idx >= t->count)
        return -1;
    t->threads[idx].state = THREAD_ABORTED;
    if (t->active_idx == idx)
        t->active_idx = -1;
    return 0;
}

void thread_mgr_record_turn(thread_mgr_t *t, int idx) {
    if (!t || idx < 0 || idx >= t->count)
        return;
    t->threads[idx].turn_count++;
    t->threads[idx].last_activity = nc_wall_sec();
}

int thread_mgr_active_count(const thread_mgr_t *t) {
    if (!t)
        return 0;
    int n = 0;
    for (int i = 0; i < t->count; i++)
        if (t->threads[i].state == THREAD_ACTIVE)
            n++;
    return n;
}

int thread_mgr_children(const thread_mgr_t *t, const char *parent_id, int *out, int max) {
    if (!t || !parent_id)
        return 0;
    int f = 0;
    for (int i = 0; i < t->count && f < max; i++)
        if (t->threads[i].parent_id && strcmp(t->threads[i].parent_id, parent_id) == 0)
            out[f++] = i;
    return f;
}

const char *thread_state_name(nc_thread_state_t s) {
    switch (s) {
    case THREAD_ACTIVE:
        return "active";
    case THREAD_SUSPENDED:
        return "suspended";
    case THREAD_COMPLETED:
        return "completed";
    case THREAD_ABORTED:
        return "aborted";
    default:
        return "unknown";
    }
}

char *thread_mgr_format(const thread_mgr_t *t) {
    if (!t || t->count == 0)
        return nc_xstrdup("No threads.\n");
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Threads (%d):\n", t->count);
    for (int i = 0; i < t->count; i++) {
        const thread_entry_t *e = &t->threads[i];
        strbuf_appendf(&sb, "  %s[%s] \"%s\" -- %s, %d turns", (t->active_idx == i) ? "> " : "  ",
                       e->id, e->label, thread_state_name(e->state), e->turn_count);
        if (e->parent_id)
            strbuf_appendf(&sb, " (parent: %s)", e->parent_id);
        if (e->last_task)
            strbuf_appendf(&sb, " task: %s", e->last_task);
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}
