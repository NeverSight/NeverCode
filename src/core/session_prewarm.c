#include "nevercode.h"
#include <curl/curl.h>

typedef enum {
    PW_IDLE = 0,
    PW_RUNNING,
    PW_READY,
    PW_FAILED,
    PW_CONSUMED,
    PW_TIMED_OUT,
    PW_CANCELLED
} prewarm_status_t;

static struct {
    pthread_t thread;
    pthread_mutex_t lock;
    prewarm_status_t status;
    CURL *handle;
    long started_ms;
    long ready_ms;
    long timeout_ms;
    char *base_url;
    char *api_key;
    provider_type_t provider;
    bool thread_created;
} g_pw;

#define pw_now_ms nc_mono_ms

static size_t pw_discard_cb(void *p, size_t s, size_t n, void *u) {
    (void)p;
    (void)u;
    return s * n;
}

static void *prewarm_thread(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_pw.lock);
    char *url = g_pw.base_url ? nc_xstrdup(g_pw.base_url) : NULL;
    char *key = g_pw.api_key ? nc_xstrdup(g_pw.api_key) : NULL;
    provider_type_t prov = g_pw.provider;
    long tmo = g_pw.timeout_ms;
    pthread_mutex_unlock(&g_pw.lock);

    if (!url) {
        pthread_mutex_lock(&g_pw.lock);
        g_pw.status = PW_FAILED;
        pthread_mutex_unlock(&g_pw.lock);
        free(key);
        return NULL;
    }

    CURL *c = curl_easy_init();
    if (!c) {
        pthread_mutex_lock(&g_pw.lock);
        g_pw.status = PW_FAILED;
        pthread_mutex_unlock(&g_pw.lock);
        free(url);
        free(key);
        return NULL;
    }

    strbuf_t probe;
    strbuf_init(&probe);
    if (prov == PROVIDER_COMPAT)
        strbuf_appendf(&probe, "%s/v1/models", url);
    else
        strbuf_appendf(&probe, "%s/v1/messages", url);

    curl_easy_setopt(c, CURLOPT_URL, probe.data);
    curl_easy_setopt(c, CURLOPT_NOBODY, (prov == PROVIDER_COMPAT) ? 0L : 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, tmo > 0 ? tmo : (long)PREWARM_TIMEOUT_MS);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS,
                     tmo > 0 ? tmo / 2 : (long)PREWARM_CONNECT_TIMEOUT_MS);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, pw_discard_cb);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);

    struct curl_slist *hdrs = NULL;
    if (key) {
        strbuf_t auth;
        strbuf_init(&auth);
        if (prov == PROVIDER_COMPAT)
            strbuf_appendf(&auth, "Authorization: Bearer %s", key);
        else
            strbuf_appendf(&auth, "x-api-key: %s", key);
        hdrs = curl_slist_append(hdrs, auth.data);
        strbuf_free(&auth);
    }
    if (hdrs)
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode res = curl_easy_perform(c);
    pthread_mutex_lock(&g_pw.lock);
    if (g_pw.status == PW_CANCELLED) {
        /* cancelled while running */
    } else if (res == CURLE_OK || res == CURLE_HTTP_RETURNED_ERROR) {
        g_pw.handle = c;
        c = NULL;
        g_pw.ready_ms = pw_now_ms();
        g_pw.status = PW_READY;
    } else if (res == CURLE_OPERATION_TIMEDOUT) {
        g_pw.status = PW_TIMED_OUT;
    } else {
        g_pw.status = PW_FAILED;
    }
    pthread_mutex_unlock(&g_pw.lock);

    if (c)
        curl_easy_cleanup(c);
    curl_slist_free_all(hdrs);
    strbuf_free(&probe);
    free(url);
    free(key);
    return NULL;
}

void session_prewarm_init(void) {
    memset(&g_pw, 0, sizeof(g_pw));
    pthread_mutex_init(&g_pw.lock, NULL);
    g_pw.status = PW_IDLE;
}

void session_prewarm_free(void) {
    session_prewarm_cancel();
    if (g_pw.handle) {
        curl_easy_cleanup(g_pw.handle);
        g_pw.handle = NULL;
    }
    free(g_pw.base_url);
    free(g_pw.api_key);
    pthread_mutex_destroy(&g_pw.lock);
    memset(&g_pw, 0, sizeof(g_pw));
}

int session_prewarm_start(const char *base_url, const char *api_key, provider_type_t provider,
                          long timeout_ms) {
    if (!base_url)
        return -1;
    pthread_mutex_lock(&g_pw.lock);
    if (g_pw.status == PW_RUNNING) {
        pthread_mutex_unlock(&g_pw.lock);
        return -1;
    }
    free(g_pw.base_url);
    free(g_pw.api_key);
    g_pw.base_url = nc_xstrdup(base_url);
    g_pw.api_key = api_key ? nc_xstrdup(api_key) : NULL;
    g_pw.provider = provider;
    g_pw.timeout_ms = timeout_ms > 0 ? timeout_ms : PREWARM_TIMEOUT_MS;
    g_pw.started_ms = pw_now_ms();
    g_pw.ready_ms = 0;
    g_pw.status = PW_RUNNING;
    pthread_mutex_unlock(&g_pw.lock);

    if (pthread_create(&g_pw.thread, NULL, prewarm_thread, NULL) != 0) {
        g_pw.status = PW_FAILED;
        return -1;
    }
    g_pw.thread_created = true;
    return 0;
}

void session_prewarm_cancel(void) {
    pthread_mutex_lock(&g_pw.lock);
    if (g_pw.status == PW_RUNNING)
        g_pw.status = PW_CANCELLED;
    pthread_mutex_unlock(&g_pw.lock);
    if (g_pw.thread_created) {
        pthread_join(g_pw.thread, NULL);
        g_pw.thread_created = false;
    }
}

bool session_prewarm_ready(void) {
    pthread_mutex_lock(&g_pw.lock);
    bool r = (g_pw.status == PW_READY);
    pthread_mutex_unlock(&g_pw.lock);
    return r;
}

CURL *session_prewarm_consume(void) {
    pthread_mutex_lock(&g_pw.lock);
    CURL *h = NULL;
    if (g_pw.status == PW_READY) {
        h = g_pw.handle;
        g_pw.handle = NULL;
        g_pw.status = PW_CONSUMED;
    }
    pthread_mutex_unlock(&g_pw.lock);
    if (g_pw.thread_created) {
        pthread_join(g_pw.thread, NULL);
        g_pw.thread_created = false;
    }
    return h;
}

long session_prewarm_age_ms(void) {
    pthread_mutex_lock(&g_pw.lock);
    long age = (g_pw.started_ms > 0) ? pw_now_ms() - g_pw.started_ms : 0;
    pthread_mutex_unlock(&g_pw.lock);
    return age;
}

long session_prewarm_latency_ms(void) {
    pthread_mutex_lock(&g_pw.lock);
    long lat = (g_pw.ready_ms > 0 && g_pw.started_ms > 0) ? g_pw.ready_ms - g_pw.started_ms : 0;
    pthread_mutex_unlock(&g_pw.lock);
    return lat;
}

const char *session_prewarm_status_name(void) {
    static const char *names[] = {"idle",     "running",   "ready",    "failed",
                                  "consumed", "timed_out", "cancelled"};
    pthread_mutex_lock(&g_pw.lock);
    int s = (int)g_pw.status;
    pthread_mutex_unlock(&g_pw.lock);
    return (s >= 0 && s <= 6) ? names[s] : "unknown";
}

char *session_prewarm_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "prewarm: %s", session_prewarm_status_name());
    long lat = session_prewarm_latency_ms();
    if (lat > 0)
        strbuf_appendf(&sb, " (latency=%ldms)", lat);
    long age = session_prewarm_age_ms();
    if (age > 0)
        strbuf_appendf(&sb, " (age=%ldms)", age);
    strbuf_append_str(&sb, "\n");
    return strbuf_detach(&sb);
}
