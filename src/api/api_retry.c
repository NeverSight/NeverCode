/*
 * api_retry.c — Enhanced API retry with exponential backoff, jitter,
 *               per-error-code handling, and Retry-After support.
 *               Distinguishes between transient and permanent failures.
 */

#include "nevercode.h"
#include "harness.h"

/* RETRY_BASE_DELAY_MS .. RETRY_JITTER_FACTOR in defaults.h */
#define BASE_DELAY_MS RETRY_BASE_DELAY_MS
#define MAX_DELAY_MS RETRY_MAX_DELAY_MS
#define MAX_RETRIES_DEFAULT RETRY_MAX_DEFAULT
#define MAX_529_RETRIES RETRY_MAX_OVERLOAD
#define JITTER_FACTOR RETRY_JITTER_FACTOR

typedef enum {
    RETRY_CLASS_NONE = 0,
    RETRY_CLASS_RATE_LIMIT = 1,  /* 429 */
    RETRY_CLASS_OVERLOADED = 2,  /* 529, 503 */
    RETRY_CLASS_SERVER_ERR = 3,  /* 500, 502 */
    RETRY_CLASS_NETWORK = 4,     /* connection failures */
    RETRY_CLASS_AUTH = 5,        /* 401, 403 — no retry */
    RETRY_CLASS_BAD_REQUEST = 6, /* 400, 404 — no retry */
    RETRY_CLASS_TIMEOUT = 7      /* 408, CURLE_OPERATION_TIMEDOUT */
} retry_class_t;

static retry_class_t classify_error(const char *error_msg, int http_status) {
    if (http_status == HTTP_RATE_LIMIT)
        return RETRY_CLASS_RATE_LIMIT;
    if (http_status == HTTP_OVERLOADED || http_status == HTTP_UNAVAILABLE)
        return RETRY_CLASS_OVERLOADED;
    if (http_status == HTTP_INTERNAL || http_status == HTTP_BAD_GATEWAY)
        return RETRY_CLASS_SERVER_ERR;
    if (http_status == HTTP_TIMEOUT)
        return RETRY_CLASS_TIMEOUT;
    if (http_status == HTTP_UNAUTHORIZED || http_status == HTTP_FORBIDDEN)
        return RETRY_CLASS_AUTH;
    if (http_status == HTTP_BAD_REQUEST || http_status == HTTP_NOT_FOUND)
        return RETRY_CLASS_BAD_REQUEST;

    if (error_msg) {
        if (strstr(error_msg, AERR_BODY_TURN_DEADLINE) ||
            strstr(error_msg, AERR_BODY_WC_DEADLINE) || strstr(error_msg, AERR_BODY_CTX_DEGRADED))
            return RETRY_CLASS_NONE;
        if (strstr(error_msg, HTTP_STR_429) || strstr(error_msg, HTTP_ERR_RATE_LIMIT))
            return RETRY_CLASS_RATE_LIMIT;
        if (strstr(error_msg, HTTP_STR_529) || strstr(error_msg, HTTP_ERR_OVERLOADED))
            return RETRY_CLASS_OVERLOADED;
        if (strstr(error_msg, HTTP_STR_503) || strstr(error_msg, HTTP_ERR_UNAVAILABLE))
            return RETRY_CLASS_OVERLOADED;
        if (strstr(error_msg, HTTP_STR_500) || strstr(error_msg, HTTP_STR_502) ||
            strstr(error_msg, HTTP_ERR_INTERNAL) || strstr(error_msg, HTTP_ERR_BAD_GW))
            return RETRY_CLASS_SERVER_ERR;
        if (strstr(error_msg, NET_ERR_CONNECTION) || strstr(error_msg, NET_ERR_CURLE_PREFIX) ||
            strstr(error_msg, NET_ERR_TIMEOUT) || strstr(error_msg, NET_ERR_TIMED_OUT) ||
            strstr(error_msg, NET_ERR_NETWORK))
            return RETRY_CLASS_NETWORK;
        if (strstr(error_msg, HTTP_STR_401) || strstr(error_msg, HTTP_ERR_UNAUTHORIZED) ||
            strstr(error_msg, HTTP_STR_403) || strstr(error_msg, HTTP_ERR_FORBIDDEN))
            return RETRY_CLASS_AUTH;
    }
    return RETRY_CLASS_NONE;
}

static bool should_retry(retry_class_t cls) {
    switch (cls) {
    case RETRY_CLASS_RATE_LIMIT:
    case RETRY_CLASS_OVERLOADED:
    case RETRY_CLASS_SERVER_ERR:
    case RETRY_CLASS_NETWORK:
    case RETRY_CLASS_TIMEOUT:
        return true;
    case RETRY_CLASS_AUTH:
    case RETRY_CLASS_BAD_REQUEST:
    case RETRY_CLASS_NONE:
        return false;
    }
    return false;
}

static int max_retries_for_class(retry_class_t cls) {
    switch (cls) {
    case RETRY_CLASS_RATE_LIMIT:
        return MAX_RETRIES_DEFAULT;
    case RETRY_CLASS_OVERLOADED:
        return MAX_529_RETRIES;
    case RETRY_CLASS_SERVER_ERR:
        return MAX_RETRIES_DEFAULT;
    case RETRY_CLASS_NETWORK:
        return MAX_RETRIES_DEFAULT;
    case RETRY_CLASS_TIMEOUT:
        return RETRY_TIMEOUT_MAX;
    default:
        return 0;
    }
}

static int add_jitter(int base_ms) {
    double jitter = nc_rand_double() * JITTER_FACTOR;
    int sign = (nc_rand_uniform(2)) ? 1 : -1;
    int delta = (int)(base_ms * jitter) * sign;
    int result = base_ms + delta;
    return result > 0 ? result : base_ms;
}

static int compute_delay(int attempt, retry_class_t cls) {
    int base = BASE_DELAY_MS;

    if (cls == RETRY_CLASS_RATE_LIMIT)
        base = RETRY_RATE_LIMIT_BASE_MS;
    else if (cls == RETRY_CLASS_OVERLOADED)
        base = RETRY_OVERLOAD_BASE_MS;

    if (attempt < 0)
        attempt = 0;

    int delay = nc_backoff_exp2_sat(base, MAX_DELAY_MS, attempt);

    int result = add_jitter(delay);
    return result > MAX_DELAY_MS ? MAX_DELAY_MS : result;
}

const char *retry_class_name(retry_class_t cls) {
    switch (cls) {
    case RETRY_CLASS_NONE:
        return "none";
    case RETRY_CLASS_RATE_LIMIT:
        return "rate_limit";
    case RETRY_CLASS_OVERLOADED:
        return "overloaded";
    case RETRY_CLASS_SERVER_ERR:
        return "server_error";
    case RETRY_CLASS_NETWORK:
        return "network";
    case RETRY_CLASS_AUTH:
        return "auth";
    case RETRY_CLASS_BAD_REQUEST:
        return "bad_request";
    case RETRY_CLASS_TIMEOUT:
        return "timeout";
    }
    return "unknown";
}

/* Retry context — passed to stream callbacks to intercept errors */
typedef struct {
    stream_callback_t real_cb;
    void *real_ud;
    retry_class_t error_class;
    char error_msg[NC_MSG_MAX];
    int retry_after_ms;
    bool had_error;
    bool had_success_data;
} retry_ctx_t;

static int parse_retry_after(const char *msg) {
    if (!msg)
        return 0;

    /* Millisecond variant first (retry-after-ms / x-ratelimit-reset-ms) */
    const char *rams = strstr(msg, "retry-after-ms:");
    if (!rams)
        rams = strstr(msg, "Retry-After-Ms:");
    if (rams) {
        rams = strchr(rams, ':');
        if (rams) {
            rams++;
            while (*rams == ' ')
                rams++;
            int ms = nc_safe_atoi(rams, 0);
            if (ms > 0)
                return ms;
        }
    }

    const char *ra = strstr(msg, "Retry-After:");
    if (!ra)
        ra = strstr(msg, "retry-after:");
    if (!ra)
        ra = strstr(msg, "retry_after");
    if (!ra)
        return 0;

    ra = strchr(ra, ':');
    if (!ra)
        return 0;
    ra++;
    while (*ra == ' ')
        ra++;

    /* Try numeric seconds first */
    int val = nc_safe_atoi(ra, 0);
    if (val > 0)
        return val * 1000;

    /* Fall back to HTTP date format (RFC 7231) —
     * e.g. "Sun, 06 Apr 2026 12:00:00 GMT" */
    struct tm tm_parsed;
    memset(&tm_parsed, 0, sizeof(tm_parsed));
    char *end = strptime(ra, "%a, %d %b %Y %H:%M:%S", &tm_parsed);
    if (end) {
        time_t target = timegm(&tm_parsed);
        if (target > 0) {
            long delta = (long)(target - nc_time_now());
            if (delta > 0 && delta < 86400)
                return (int)(delta * 1000);
        }
    }

    return 0;
}

static void retry_intercept_cb(const stream_event_t *ev, void *ud) {
    retry_ctx_t *ctx = ud;

    if (ev->type == STREAM_EVENT_ERROR && ev->error_message) {
        ctx->had_error = true;
        int extracted_status = 0;
        if (nc_starts_with(ev->error_message, "HTTP "))
            extracted_status = nc_safe_atoi(ev->error_message + 5, 0);
        ctx->error_class = classify_error(ev->error_message, extracted_status);
        nc_strlcpy(ctx->error_msg, ev->error_message, sizeof(ctx->error_msg));
        ctx->retry_after_ms = parse_retry_after(ev->error_message);

        harness_classify_failure(ev->error_message, extracted_status);

        if (!should_retry(ctx->error_class)) {
            ctx->real_cb(ev, ctx->real_ud);
        }
        return;
    }

    if (ev->type == STREAM_EVENT_TEXT_DELTA || ev->type == STREAM_EVENT_TOOL_USE_BEGIN)
        ctx->had_success_data = true;

    ctx->real_cb(ev, ctx->real_ud);
}

int api_retry_stream(int (*stream_fn)(const message_t *, int, const char *, const void *,
                                      const tool_def_t *, int, stream_callback_t, void *),
                     const message_t *msgs, int n, const char *sys, const void *cfg,
                     const tool_def_t *tools, int nt, stream_callback_t cb, void *ud) {
    (void)0; /* nc_rand_* requires no seeding */

    int class_counts[RETRY_CLASS_COUNT] = {0};

    for (int attempt = 0; attempt < MAX_RETRIES_DEFAULT + 1; attempt++) {
        retry_ctx_t rctx = {.real_cb = cb,
                            .real_ud = ud,
                            .error_class = RETRY_CLASS_NONE,
                            .retry_after_ms = 0,
                            .had_error = false,
                            .had_success_data = false};

        int rc = stream_fn(msgs, n, sys, cfg, tools, nt, retry_intercept_cb, &rctx);

        if (rc == 0 && !rctx.had_error)
            return 0;

        if (rctx.had_success_data)
            return rc;

        if (!should_retry(rctx.error_class))
            return rc;

        class_counts[rctx.error_class]++;
        int max_for_class = max_retries_for_class(rctx.error_class);
        if (class_counts[rctx.error_class] > max_for_class)
            return rc;

        int delay_ms = rctx.retry_after_ms > 0 ? rctx.retry_after_ms
                                               : compute_delay(attempt, rctx.error_class);

        fprintf(stderr, "[retry] %s (attempt %d/%d, waiting %dms)\n",
                retry_class_name(rctx.error_class), class_counts[rctx.error_class], max_for_class,
                delay_ms);
        usleep((unsigned)(delay_ms * 1000));
    }
    return -1;
}

int api_retry_classify(const char *error_msg) {
    return (int)classify_error(error_msg, 0);
}

bool api_retry_should_retry(int error_class) {
    return should_retry((retry_class_t)error_class);
}

int api_retry_delay_ms(int attempt, int error_class) {
    return compute_delay(attempt, (retry_class_t)error_class);
}
