/*
 * parallel_exec.c — bounded concurrent tool runs
 */

#include "defaults.h"
#include "nevercode.h"
typedef enum { PX_EMPTY = 0, PX_PENDING, PX_RUNNING, PX_DONE, PX_ERR } pex_slot_state_t;

typedef struct {
    pex_slot_state_t state;
    const tool_def_t *tool;
    cJSON *input;
    char *output;
    int rc;
    long elapsed_ms;
    pthread_t thr;
    bool thr_valid;
} pex_slot_t;

static pthread_once_t px_once = PTHREAD_ONCE_INIT;

static struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int inited;
    int max_conc;
    long timeout_ms;
    void *tool_ctx;
    pex_slot_t slots[PARALLEL_EXEC_MAX_SLOTS];
    int n;
    int active;
    int completed;
    int failed;
} g_px;

static void px_once_init(void) {
    pthread_mutex_init(&g_px.mu, NULL);
    pthread_cond_init(&g_px.cv, NULL);
    g_px.inited = 1;
}

static void *px_worker(void *arg) {
    pex_slot_t *sl = (pex_slot_t *)arg;
    long t0 = nc_mono_ms();
    pthread_mutex_lock(&g_px.mu);
    sl->state = PX_RUNNING;
    pthread_mutex_unlock(&g_px.mu);

    char *out = NULL;
    int rc = -1;
    if (sl->tool && sl->tool->execute)
        rc = sl->tool->execute(sl->input, &out, g_px.tool_ctx);

    pthread_mutex_lock(&g_px.mu);
    sl->output = out;
    sl->rc = rc;
    sl->elapsed_ms = nc_mono_ms() - t0;
    sl->state = (rc == 0) ? PX_DONE : PX_ERR;
    g_px.completed++;
    if (rc != 0)
        g_px.failed++;
    g_px.active--;
    pthread_cond_broadcast(&g_px.cv);
    pthread_mutex_unlock(&g_px.mu);
    return NULL;
}

void pexec_init(int max_concurrent, long timeout_ms, void *tool_ctx) {
    pthread_once(&px_once, px_once_init);
    pexec_free();
    g_px.max_conc = max_concurrent > 0 ? max_concurrent : PARALLEL_EXEC_DEFAULT_CONCURRENT;
    g_px.timeout_ms = timeout_ms;
    g_px.tool_ctx = tool_ctx;
}

void pexec_free(void) {
    if (!g_px.inited)
        return;
    pthread_mutex_lock(&g_px.mu);
    for (int i = 0; i < g_px.n; i++) {
        if (g_px.slots[i].thr_valid)
            pthread_join(g_px.slots[i].thr, NULL);
        free(g_px.slots[i].output);
        cJSON_Delete(g_px.slots[i].input);
        memset(&g_px.slots[i], 0, sizeof(g_px.slots[i]));
    }
    g_px.n = g_px.active = g_px.completed = g_px.failed = 0;
    pthread_mutex_unlock(&g_px.mu);
}

int pexec_enqueue(const tool_def_t *tool, cJSON *input) {
    if (!tool || !input)
        return -1;
    pthread_once(&px_once, px_once_init);
    pthread_mutex_lock(&g_px.mu);
    if (g_px.n >= PARALLEL_EXEC_MAX_SLOTS) {
        pthread_mutex_unlock(&g_px.mu);
        return -1;
    }
    pex_slot_t *s = &g_px.slots[g_px.n];
    memset(s, 0, sizeof(*s));
    s->tool = tool;
    s->input = cJSON_Duplicate(input, 1);
    if (!s->input) {
        pthread_mutex_unlock(&g_px.mu);
        return -1;
    }
    s->state = PX_PENDING;
    g_px.n++;
    pthread_mutex_unlock(&g_px.mu);
    return 0;
}

int pexec_launch_all(void) {
    pthread_once(&px_once, px_once_init);
    pthread_mutex_lock(&g_px.mu);
    g_px.completed = 0;
    g_px.failed = 0;
    for (int i = 0; i < g_px.n; i++) {
        while (g_px.active >= g_px.max_conc)
            pthread_cond_wait(&g_px.cv, &g_px.mu);
        pex_slot_t *s = &g_px.slots[i];
        if (pthread_create(&s->thr, NULL, px_worker, s) != 0) {
            s->state = PX_ERR;
            s->rc = -1;
            s->output = nc_xstrdup("[pexec: thread create failed]");
            g_px.failed++;
            g_px.completed++;
            continue;
        }
        s->thr_valid = true;
        g_px.active++;
    }
    pthread_mutex_unlock(&g_px.mu);
    return 0;
}

int pexec_wait_all(void) {
    for (int i = 0; i < g_px.n; i++) {
        pex_slot_t *s = &g_px.slots[i];
        if (s->thr_valid) {
            pthread_join(s->thr, NULL);
            s->thr_valid = false;
        }
    }
    return 0;
}

int pexec_done_count(void) {
    pthread_mutex_lock(&g_px.mu);
    int c = g_px.completed;
    pthread_mutex_unlock(&g_px.mu);
    return c;
}

bool pexec_all_done(void) {
    pthread_mutex_lock(&g_px.mu);
    bool ok = (g_px.n > 0 && g_px.completed >= g_px.n);
    pthread_mutex_unlock(&g_px.mu);
    return ok;
}

const char *pexec_slot_output(int idx) {
    if (idx < 0 || idx >= g_px.n)
        return NULL;
    return g_px.slots[idx].output;
}

int pexec_slot_rc(int idx) {
    if (idx < 0 || idx >= g_px.n)
        return -1;
    return g_px.slots[idx].rc;
}
