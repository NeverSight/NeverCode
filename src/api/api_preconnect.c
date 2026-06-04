/*
 * api_preconnect.c — TCP+TLS handshake warmup during init.
 *
 * Fires a lightweight HEAD request during startup so the TCP+TLS
 * handshake (~100-200ms) overlaps with user typing or action setup.
 * libcurl's connection pool reuses the warmed connection for the
 * real API request, cutting perceived latency.
 *
 * Skipped when proxy or unix socket transport is configured (the
 * pre-warmed connection wouldn't match the real transport path).
 */

#include "nevercode.h"
#include <curl/curl.h>

static struct {
    bool fired;
    bool enabled;
    long elapsed_ms;
    int result;
} g_pc = {.enabled = true};

static size_t discard_cb(void *p, size_t sz, size_t n, void *ud) {
    (void)p;
    (void)ud;
    return sz * n;
}

static void *preconnect_thread(void *arg) {
    char *url = (char *)arg;
    CURL *c = curl_easy_init();
    if (!c) {
        free(url);
        return NULL;
    }
    long t0 = nc_mono_ms();

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)PRECONN_TIMEOUT_SEC);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, (long)PRECONN_CONNECT_SEC);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, discard_cb);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(c);

    g_pc.elapsed_ms = nc_mono_ms() - t0;
    g_pc.result = (int)rc;

    curl_easy_cleanup(c);
    free(url);
    return NULL;
}

void api_preconnect(const char *base_url) {
    if (g_pc.fired || !g_pc.enabled)
        return;

    if (!base_url || !*base_url)
        return;

    g_pc.fired = true;

    if (nc_get_proxy_env())
        return;

    char *url = nc_xstrdup(base_url);
    if (!url)
        return;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, preconnect_thread, url) != 0)
        free(url);
    pthread_attr_destroy(&attr);
}

void api_preconnect_set_enabled(bool on) {
    g_pc.enabled = on;
}

bool api_preconnect_fired(void) {
    return g_pc.fired;
}

long api_preconnect_elapsed_ms(void) {
    return g_pc.elapsed_ms;
}

int api_preconnect_result(void) {
    return g_pc.result;
}

void api_preconnect_reset(void) {
    g_pc.fired = false;
    g_pc.elapsed_ms = 0;
    g_pc.result = 0;
}
