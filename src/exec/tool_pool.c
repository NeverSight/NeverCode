#include "nevercode.h"
#include "defaults.h"

#define TP_MAX PARALLEL_EXEC_MAX_SLOTS

typedef enum { SL_FREE = 0, SL_BUSY, SL_DONE, SL_ERROR } sl_st_t;

typedef struct {
    sl_st_t state;
    char tool_name[TOOL_POOL_NAME_MAX];
    char *input;
    char *output;
    int output_len, output_cap;
    int rc;
    time_t t_start, t_end;
} tp_slot_t;

static struct {
    tp_slot_t slots[TP_MAX];
    int cap, active;
    pthread_mutex_t lock;
    bool init;
} g_tp;

void tpool_init(int cap) {
    memset(&g_tp, 0, sizeof(g_tp));
    g_tp.cap = (cap > 0 && cap <= TP_MAX) ? cap : PARALLEL_EXEC_DEFAULT_CONCURRENT;
    pthread_mutex_init(&g_tp.lock, NULL);
    g_tp.init = true;
}

void tpool_cleanup(void) {
    if (!g_tp.init)
        return;
    pthread_mutex_lock(&g_tp.lock);
    for (int i = 0; i < TP_MAX; i++) {
        free(g_tp.slots[i].input);
        free(g_tp.slots[i].output);
        memset(&g_tp.slots[i], 0, sizeof(tp_slot_t));
    }
    g_tp.active = 0;
    pthread_mutex_unlock(&g_tp.lock);
    pthread_mutex_destroy(&g_tp.lock);
    g_tp.init = false;
}

int tpool_acquire(const char *tn) {
    if (!g_tp.init)
        return -1;
    pthread_mutex_lock(&g_tp.lock);
    if (g_tp.active >= g_tp.cap) {
        pthread_mutex_unlock(&g_tp.lock);
        return -1;
    }
    for (int i = 0; i < TP_MAX; i++) {
        if (g_tp.slots[i].state == SL_FREE) {
            tp_slot_t *s = &g_tp.slots[i];
            s->state = SL_BUSY;
            nc_strlcpy(s->tool_name, tn ? tn : "", TOOL_POOL_NAME_MAX);
            s->t_start = nc_time_now();
            g_tp.active++;
            pthread_mutex_unlock(&g_tp.lock);
            return i;
        }
    }
    pthread_mutex_unlock(&g_tp.lock);
    return -1;
}

int tpool_complete(int sl, const char *out, int ol, int rc) {
    if (!g_tp.init || sl < 0 || sl >= TP_MAX)
        return -1;
    pthread_mutex_lock(&g_tp.lock);
    tp_slot_t *s = &g_tp.slots[sl];
    if (s->state != SL_BUSY) {
        pthread_mutex_unlock(&g_tp.lock);
        return -1;
    }
    free(s->output);
    s->output = NULL;
    if (out && ol > 0) {
        s->output = malloc(ol + 1);
        if (s->output) {
            memcpy(s->output, out, ol);
            s->output[ol] = '\0';
        }
        s->output_len = ol;
    }
    s->rc = rc;
    s->t_end = nc_time_now();
    s->state = (rc == 0) ? SL_DONE : SL_ERROR;
    pthread_mutex_unlock(&g_tp.lock);
    return 0;
}

int tpool_release(int sl) {
    if (!g_tp.init || sl < 0 || sl >= TP_MAX)
        return -1;
    pthread_mutex_lock(&g_tp.lock);
    tp_slot_t *s = &g_tp.slots[sl];
    free(s->input);
    free(s->output);
    memset(s, 0, sizeof(*s));
    if (g_tp.active > 0)
        g_tp.active--;
    pthread_mutex_unlock(&g_tp.lock);
    return 0;
}

int tpool_active_count(void) {
    if (!g_tp.init)
        return 0;
    pthread_mutex_lock(&g_tp.lock);
    int n = g_tp.active;
    pthread_mutex_unlock(&g_tp.lock);
    return n;
}

int tpool_capacity(void) {
    return g_tp.init ? g_tp.cap : 0;
}
bool tpool_slot_busy(int sl) {
    return g_tp.init && sl >= 0 && sl < TP_MAX && g_tp.slots[sl].state == SL_BUSY;
}
int tpool_slot_rc(int sl) {
    return (g_tp.init && sl >= 0 && sl < TP_MAX) ? g_tp.slots[sl].rc : -1;
}

const char *tpool_slot_output(int sl, int *ol) {
    if (!g_tp.init || sl < 0 || sl >= TP_MAX)
        return NULL;
    if (ol)
        *ol = g_tp.slots[sl].output_len;
    return g_tp.slots[sl].output;
}

double tpool_slot_elapsed(int sl) {
    if (!g_tp.init || sl < 0 || sl >= TP_MAX)
        return 0.0;
    tp_slot_t *s = &g_tp.slots[sl];
    return s->t_end ? difftime(s->t_end, s->t_start) : difftime(nc_time_now(), s->t_start);
}
