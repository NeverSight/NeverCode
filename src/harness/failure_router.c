/*
 * failure_router.c — Failure classification, routing, and adaptive rate control
 *
 * Classifies errors into categories and determines retry strategy
 * with exponential backoff, jitter, and adaptive rate scaling.
 * Tracks per-class error frequency to dynamically increase backoff
 * when a provider is under sustained pressure.
 */

#include "harness.h"

/* FR_WINDOW_SIZE, FR_SCALE_FLOOR, FR_SCALE_CEILING in defaults.h */

typedef struct {
    long timestamps[FR_WINDOW_SIZE];
    int head;
    int count;
    double adaptive_scale;
} fr_class_history_t;

static fr_class_history_t g_hist[FAIL_CLASS_COUNT];

static void recalc_adaptive_scale(fr_class_history_t *h, long now_s) {
    if (!h)
        return;
    h->adaptive_scale = FR_SCALE_FLOOR;
    if (h->count < 3)
        return;
    int oldest = (h->head - h->count + FR_WINDOW_SIZE) % FR_WINDOW_SIZE;
    long oldest_ts = h->timestamps[oldest];
    if (oldest_ts <= 0)
        return;
    long span = now_s - oldest_ts;
    if (span <= 0)
        span = 1;
    double rate = (double)h->count / (double)span;
    h->adaptive_scale = FR_SCALE_FLOOR + rate * 2.0;
    if (h->adaptive_scale > FR_SCALE_CEILING)
        h->adaptive_scale = FR_SCALE_CEILING;
}

void failure_router_init(void) {
    memset(g_hist, 0, sizeof(g_hist));
    for (int i = 0; i < FAIL_CLASS_COUNT; i++)
        g_hist[i].adaptive_scale = FR_SCALE_FLOOR;
}

void failure_router_free(void) {
    memset(g_hist, 0, sizeof(g_hist));
}

static void record_failure_event(failure_class_t cls) {
    if ((int)cls < 0 || (int)cls >= 5)
        return;
    fr_class_history_t *h = &g_hist[(int)cls];
    long now_s = nc_wall_sec();
    h->timestamps[h->head] = now_s;
    h->head = (h->head + 1) % FR_WINDOW_SIZE;
    if (h->count < FR_WINDOW_SIZE)
        h->count++;
    recalc_adaptive_scale(h, now_s);
}

failure_class_t harness_classify_failure(const char *error_msg, int http_status) {
    if (!error_msg)
        return FAIL_FATAL;

    failure_class_t cls;

    if (http_status == HTTP_RATE_LIMIT || strstr(error_msg, HTTP_ERR_RATE_LIMIT) ||
        strstr(error_msg, HTTP_ERR_TOO_MANY))
        cls = FAIL_TRANSIENT;
    else if (http_status == HTTP_BAD_GATEWAY || http_status == HTTP_UNAVAILABLE ||
             http_status == HTTP_GATEWAY_TIMEOUT || strstr(error_msg, HTTP_ERR_OVERLOADED) ||
             strstr(error_msg, HTTP_ERR_TEMPORARILY) || strstr(error_msg, NET_ERR_TIMEOUT) ||
             strstr(error_msg, NET_ERR_CONNRESET))
        cls = FAIL_TRANSIENT;
    else if (strstr(error_msg, AERR_BODY_PROMPT_LONG) ||
             strstr(error_msg, HTTP_ERR_CONTEXT_LENGTH) || strstr(error_msg, HTTP_ERR_MAX_TOKENS) ||
             strstr(error_msg, HTTP_ERR_TOKEN_LIMIT) || strstr(error_msg, AERR_BODY_TOO_LONG))
        cls = FAIL_RESOURCE;
    else if (http_status == HTTP_BAD_REQUEST &&
             (strstr(error_msg, "invalid") || strstr(error_msg, AERR_BODY_MALFORMED)))
        cls = FAIL_SEMANTIC;
    else if (http_status == HTTP_UNAUTHORIZED || http_status == HTTP_FORBIDDEN ||
             strstr(error_msg, "auth") || strstr(error_msg, "key") ||
             strstr(error_msg, "permission"))
        cls = FAIL_FATAL;
    else if (http_status >= HTTP_INTERNAL)
        cls = FAIL_PERSISTENT;
    else if (http_status >= HTTP_BAD_REQUEST)
        cls = FAIL_SEMANTIC;
    else
        cls = FAIL_TRANSIENT;

    record_failure_event(cls);
    return cls;
}

const char *harness_failure_name(failure_class_t cls) {
    switch (cls) {
    case FAIL_TRANSIENT:
        return "transient";
    case FAIL_PERSISTENT:
        return "persistent";
    case FAIL_RESOURCE:
        return "resource";
    case FAIL_SEMANTIC:
        return "semantic";
    case FAIL_FATAL:
        return "fatal";
    default:
        return "unknown";
    }
}

bool harness_should_retry(failure_class_t cls, int attempt) {
    switch (cls) {
    case FAIL_TRANSIENT:
        return attempt < HARNESS_RETRY_TRANSIENT_MAX;
    case FAIL_PERSISTENT:
        return attempt < HARNESS_RETRY_PERSISTENT_MAX;
    case FAIL_RESOURCE:
        return attempt < HARNESS_MAX_HANDOFF_RETRIES;
    case FAIL_SEMANTIC:
        return attempt < HARNESS_RETRY_SEMANTIC_MAX;
    case FAIL_FATAL:
        return false;
    default:
        return false;
    }
}

int harness_retry_delay_ms(failure_class_t cls, int attempt) {
    int base;
    switch (cls) {
    case FAIL_TRANSIENT:
        base = HARNESS_RETRY_BASE_TRANSIENT;
        break;
    case FAIL_PERSISTENT:
        base = HARNESS_RETRY_BASE_PERSISTENT;
        break;
    case FAIL_RESOURCE:
        base = HARNESS_RETRY_BASE_RESOURCE;
        break;
    case FAIL_SEMANTIC:
        base = HARNESS_RETRY_BASE_SEMANTIC;
        break;
    default:
        return 0;
    }

    double scale =
        ((int)cls >= 0 && (int)cls < 5) ? g_hist[(int)cls].adaptive_scale : FR_SCALE_FLOOR;

    if (attempt < 0)
        attempt = 0;

    long long scaled_base = (long long)((double)base * scale);
    if (scaled_base < base)
        scaled_base = base;
    if (scaled_base > HARNESS_RETRY_MAX_DELAY)
        scaled_base = HARNESS_RETRY_MAX_DELAY;

    int delay = nc_backoff_exp2_sat((int)scaled_base, HARNESS_RETRY_MAX_DELAY, attempt);
    int jitter_span = delay / FR_JITTER_DIVISOR;
    if (jitter_span < 1)
        jitter_span = 1;
    int jitter = (int)nc_rand_uniform((uint32_t)(jitter_span * 2 + 1)) - jitter_span;
    delay += jitter;

    if (cls == FAIL_TRANSIENT && scale >= FR_BURST_SCALE_THRESHOLD) {
        int burst_floor = base * FR_BURST_MIN_MULTIPLIER;
        if (burst_floor > HARNESS_RETRY_MAX_DELAY)
            burst_floor = HARNESS_RETRY_MAX_DELAY;
        if (delay < burst_floor)
            delay = burst_floor;
    }

    if (delay > HARNESS_RETRY_MAX_DELAY)
        delay = HARNESS_RETRY_MAX_DELAY;
    if (delay < base)
        delay = base;
    return delay;
}

double harness_adaptive_scale(failure_class_t cls) {
    if ((int)cls < 0 || (int)cls >= 5)
        return FR_SCALE_FLOOR;
    return g_hist[(int)cls].adaptive_scale;
}

int harness_failure_window_count(failure_class_t cls) {
    if ((int)cls < 0 || (int)cls >= 5)
        return 0;
    return g_hist[(int)cls].count;
}

void failure_router_snapshot(failure_router_snapshot_t *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < FAIL_CLASS_COUNT; i++) {
        memcpy(out->classes[i].timestamps, g_hist[i].timestamps, sizeof(g_hist[i].timestamps));
        out->classes[i].head = g_hist[i].head;
        out->classes[i].count = g_hist[i].count;
    }
}

void failure_router_restore(const failure_router_snapshot_t *snap) {
    failure_router_init();
    if (!snap)
        return;

    long now_s = nc_wall_sec();
    for (int i = 0; i < FAIL_CLASS_COUNT; i++) {
        g_hist[i].head = snap->classes[i].head;
        if (g_hist[i].head < 0)
            g_hist[i].head = 0;
        g_hist[i].head %= FR_WINDOW_SIZE;

        g_hist[i].count = snap->classes[i].count;
        if (g_hist[i].count < 0)
            g_hist[i].count = 0;
        if (g_hist[i].count > FR_WINDOW_SIZE)
            g_hist[i].count = FR_WINDOW_SIZE;

        memcpy(g_hist[i].timestamps, snap->classes[i].timestamps, sizeof(g_hist[i].timestamps));
        recalc_adaptive_scale(&g_hist[i], now_s);
    }
}
