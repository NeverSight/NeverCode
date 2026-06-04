/*
 * api_error.c — Structured API error classification and mapping
 *
 * Maps HTTP status codes and response bodies to typed error
 * categories with retry guidance and user-facing messages.
 */

#include "nevercode.h"

/* AERR_MSG_MAX in defaults.h */

typedef enum {
    AERR_NONE,
    AERR_CONTEXT_EXCEEDED,
    AERR_QUOTA_EXCEEDED,
    AERR_RATE_LIMITED,
    AERR_OVERLOADED,
    AERR_INVALID_REQUEST,
    AERR_INVALID_IMAGE,
    AERR_AUTH_FAILED,
    AERR_INTERNAL_SERVER,
    AERR_NETWORK,
    AERR_STREAM_ERROR,
    AERR_TIMEOUT,
    AERR_UNKNOWN
} api_error_kind_t;

typedef struct {
    api_error_kind_t kind;
    int http_status;
    char message[AERR_MSG_MAX];
    bool retryable;
    int retry_delay_ms;
} api_error_t;

static const char *aerr_kind_names[] = {"none",          "context_exceeded", "quota_exceeded",
                                        "rate_limited",  "overloaded",       "invalid_request",
                                        "invalid_image", "auth_failed",      "internal_server",
                                        "network",       "stream_error",     "timeout",
                                        "unknown"};

void aerr_init(api_error_t *err) {
    memset(err, 0, sizeof(*err));
}

static bool body_contains(const char *body, const char *needle) {
    return body && needle && strstr(body, needle) != NULL;
}

int aerr_classify(api_error_t *err, int http_status, const char *body) {
    if (!err)
        return -1;
    aerr_init(err);
    err->http_status = http_status;

    if (http_status >= HTTP_OK && http_status < 300)
        return 0;

    if (http_status == HTTP_BAD_REQUEST) {
        if (body_contains(body, AERR_BODY_CTX_EXCEEDED) || body_contains(body, AERR_BODY_CTX_MAX)) {
            err->kind = AERR_CONTEXT_EXCEEDED;
            snprintf(err->message, sizeof(err->message),
                     "Context window exceeded. Try compacting the conversation.");
        } else if (body_contains(body, AERR_BODY_INVALID_IMAGE)) {
            err->kind = AERR_INVALID_IMAGE;
            snprintf(err->message, sizeof(err->message), "Invalid image in request.");
        } else {
            err->kind = AERR_INVALID_REQUEST;
            snprintf(err->message, sizeof(err->message), "Invalid request: %s",
                     body ? body : "(no details)");
        }
    } else if (http_status == HTTP_UNAUTHORIZED || http_status == HTTP_FORBIDDEN) {
        err->kind = AERR_AUTH_FAILED;
        snprintf(err->message, sizeof(err->message), "Authentication failed. Check your API key.");
    } else if (http_status == HTTP_RATE_LIMIT) {
        if (body_contains(body, AERR_BODY_USAGE_LIMIT) || body_contains(body, AERR_BODY_QUOTA)) {
            err->kind = AERR_QUOTA_EXCEEDED;
            snprintf(err->message, sizeof(err->message), "Usage quota exceeded.");
        } else {
            err->kind = AERR_RATE_LIMITED;
            err->retryable = true;
            err->retry_delay_ms = AERR_DELAY_RATE_LIMIT;
            snprintf(err->message, sizeof(err->message), "Rate limited. Will retry automatically.");
        }
    } else if (http_status == HTTP_INTERNAL) {
        err->kind = AERR_INTERNAL_SERVER;
        err->retryable = true;
        err->retry_delay_ms = AERR_DELAY_INTERNAL;
        snprintf(err->message, sizeof(err->message), "Internal server error.");
    } else if (http_status == HTTP_UNAVAILABLE || http_status == HTTP_OVERLOADED) {
        if (body_contains(body, AERR_BODY_OVERLOADED) || body_contains(body, AERR_BODY_SLOW_DOWN) ||
            body_contains(body, HTTP_ERR_OVERLOADED)) {
            err->kind = AERR_OVERLOADED;
            err->retryable = true;
            err->retry_delay_ms = AERR_DELAY_OVERLOADED;
            snprintf(err->message, sizeof(err->message),
                     "Server overloaded. Will retry after cooldown.");
        } else {
            err->kind = AERR_INTERNAL_SERVER;
            err->retryable = true;
            err->retry_delay_ms = AERR_DELAY_UNAVAILABLE;
            snprintf(err->message, sizeof(err->message), "Service unavailable.");
        }
    } else {
        err->kind = AERR_UNKNOWN;
        snprintf(err->message, sizeof(err->message), "Unexpected HTTP %d", http_status);
    }

    return 1;
}

int aerr_classify_stream(api_error_t *err, const char *error_msg) {
    if (!err)
        return -1;
    aerr_init(err);

    if (!error_msg) {
        err->kind = AERR_STREAM_ERROR;
        snprintf(err->message, sizeof(err->message), "Stream error (no details).");
        return 1;
    }

    if (strstr(error_msg, HTTP_ERR_OVERLOADED) || strstr(error_msg, HTTP_ERR_RATE_LIMIT) ||
        strstr(error_msg, HTTP_STR_529) || strstr(error_msg, HTTP_STR_503)) {
        err->kind = AERR_OVERLOADED;
        err->retryable = true;
        err->retry_delay_ms = AERR_DELAY_OVERLOADED;
    } else if (strstr(error_msg, NET_ERR_TIMEOUT) || strstr(error_msg, NET_ERR_ETIMEDOUT)) {
        err->kind = AERR_TIMEOUT;
        err->retryable = true;
        err->retry_delay_ms = AERR_DELAY_TIMEOUT;
    } else if (strstr(error_msg, NET_ERR_CONNREFUSED) || strstr(error_msg, NET_ERR_NETWORK)) {
        err->kind = AERR_NETWORK;
        err->retryable = true;
        err->retry_delay_ms = AERR_DELAY_NETWORK;
    } else {
        err->kind = AERR_STREAM_ERROR;
        err->retryable = true;
        err->retry_delay_ms = AERR_DELAY_STREAM_DEFAULT;
    }

    nc_strlcpy(err->message, error_msg, sizeof(err->message));
    return 1;
}

const char *aerr_kind_name(api_error_kind_t kind) {
    if (kind < 0 || kind >= NC_ARRAY_LEN(aerr_kind_names))
        return "unknown";
    return aerr_kind_names[kind];
}

bool aerr_is_retryable(const api_error_t *err) {
    return err && err->retryable;
}

bool aerr_has_error(const api_error_t *err) {
    return err && err->kind != AERR_NONE;
}

char *aerr_format(const api_error_t *err) {
    if (!err || err->kind == AERR_NONE)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "API error [%s] HTTP %d: %s", aerr_kind_name(err->kind), err->http_status,
                   err->message);
    if (err->retryable)
        strbuf_appendf(&sb, " (retry in %dms)", err->retry_delay_ms);
    return strbuf_detach(&sb);
}
