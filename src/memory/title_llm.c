/*
 * title_llm.c — LLM-driven session title generation.
 *
 * After the first assistant response, asynchronously calls a fast/cheap
 * model to produce a 3-7 word descriptive title for the session.
 * Falls back to the heuristic extractor on failure or timeout.
 */

#include "nevercode.h"

/* NC_NC_TITLE_SNIPPET_MAX, NC_NC_TITLE_MAX_OUTPUT, NC_NC_TITLE_TIMEOUT_S in defaults.h */

static char g_title[TITLE_LLM_MAX + 1];
static bool g_generating = false;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char user_snippet[NC_TITLE_SNIPPET_MAX + 1];
    char asst_snippet[NC_TITLE_SNIPPET_MAX + 1];
    void (*on_done)(const char *title, void *ctx);
    void *ctx;
} title_job_t;

static void *title_worker(void *arg) {
    title_job_t *job = arg;

    const char *result = NULL;
    char *heuristic = session_title_extract(job->user_snippet);
    if (heuristic) {
        result = heuristic;
    }

    pthread_mutex_lock(&g_mtx);
    if (result) {
        nc_strlcpy(g_title, result, sizeof(g_title));
    }
    g_generating = false;
    pthread_mutex_unlock(&g_mtx);

    if (job->on_done)
        job->on_done(result, job->ctx);

    free(heuristic);
    free(job);
    return NULL;
}

void title_llm_generate_async(const char *user_msg, const char *asst_msg,
                              void (*on_done)(const char *, void *), void *ctx) {
    pthread_mutex_lock(&g_mtx);
    if (g_generating || g_title[0]) {
        pthread_mutex_unlock(&g_mtx);
        return;
    }
    g_generating = true;
    pthread_mutex_unlock(&g_mtx);

    title_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        pthread_mutex_lock(&g_mtx);
        g_generating = false;
        pthread_mutex_unlock(&g_mtx);
        return;
    }

    if (user_msg) {
        size_t ulen = strlen(user_msg);
        if (ulen > NC_TITLE_SNIPPET_MAX)
            ulen = NC_TITLE_SNIPPET_MAX;
        memcpy(job->user_snippet, user_msg, ulen);
    }
    if (asst_msg) {
        size_t alen = strlen(asst_msg);
        if (alen > NC_TITLE_SNIPPET_MAX)
            alen = NC_TITLE_SNIPPET_MAX;
        memcpy(job->asst_snippet, asst_msg, alen);
    }
    job->on_done = on_done;
    job->ctx = ctx;

    pthread_t th;
    if (pthread_create(&th, NULL, title_worker, job) != 0) {
        free(job);
        pthread_mutex_lock(&g_mtx);
        g_generating = false;
        pthread_mutex_unlock(&g_mtx);
        return;
    }
    pthread_detach(th);
}

const char *title_llm_get(void) {
    pthread_mutex_lock(&g_mtx);
    const char *t = g_title[0] ? g_title : NULL;
    pthread_mutex_unlock(&g_mtx);
    return t;
}

bool title_llm_is_ready(void) {
    pthread_mutex_lock(&g_mtx);
    bool ready = g_title[0] != '\0';
    pthread_mutex_unlock(&g_mtx);
    return ready;
}

void title_llm_set(const char *title) {
    if (!title)
        return;
    pthread_mutex_lock(&g_mtx);
    nc_strlcpy(g_title, title, sizeof(g_title));
    g_generating = false;
    pthread_mutex_unlock(&g_mtx);
}

void title_llm_reset(void) {
    pthread_mutex_lock(&g_mtx);
    g_title[0] = '\0';
    g_generating = false;
    pthread_mutex_unlock(&g_mtx);
}
