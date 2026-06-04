/*
 * stream_event.c — Stream event processing utilities
 *
 * Handles SSE (Server-Sent Events) stream processing including:
 * - Event classification and routing
 * - Content delta accumulation
 * - Tool call extraction from streaming responses
 * - Image artifact management for generated images
 */

#include "nevercode.h"

/* Macros sourced from defaults.h: SEV_MAX_DELTAS, SEV_DELTA_BUF_SIZE,
 * SEV_IMG_ARTIFACT_DIR */

typedef enum {
    SEV_CONTENT_DELTA,
    SEV_TOOL_CALL_DELTA,
    SEV_TOOL_CALL_DONE,
    SEV_FUNCTION_CALL,
    SEV_FUNCTION_OUTPUT,
    SEV_REASONING,
    SEV_IMAGE_GEN,
    SEV_DONE,
    SEV_ERROR,
    SEV_UNKNOWN
} stream_event_kind_t;

typedef struct {
    stream_event_kind_t kind;
    int index;
    char *call_id;
    char *name;
    char *content;
    int content_len;
} stream_delta_t;

typedef struct {
    stream_delta_t deltas[SEV_MAX_DELTAS];
    int count;
    strbuf_t content_accum;
    strbuf_t reasoning_accum;
    int tool_call_count;
    bool done;
    bool error;
    char *finish_reason;
} stream_event_ctx_t;

void stream_event_init(stream_event_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    strbuf_init(&ctx->content_accum);
    strbuf_init(&ctx->reasoning_accum);
}

void stream_event_free(stream_event_ctx_t *ctx) {
    if (!ctx)
        return;
    for (int i = 0; i < ctx->count; i++) {
        free(ctx->deltas[i].call_id);
        free(ctx->deltas[i].name);
        free(ctx->deltas[i].content);
    }
    strbuf_free(&ctx->content_accum);
    strbuf_free(&ctx->reasoning_accum);
    free(ctx->finish_reason);
    memset(ctx, 0, sizeof(*ctx));
}

static stream_event_kind_t classify_event(const cJSON *event) {
    const char *t = nc_cjson_str(event, "type", NULL);
    if (!t)
        return SEV_UNKNOWN;
    if (strstr(t, "content") && strstr(t, "delta"))
        return SEV_CONTENT_DELTA;
    if (strstr(t, "tool_call") && strstr(t, "delta"))
        return SEV_TOOL_CALL_DELTA;
    if (strstr(t, "tool_call") && strstr(t, "done"))
        return SEV_TOOL_CALL_DONE;
    if (strstr(t, "function_call_output"))
        return SEV_FUNCTION_OUTPUT;
    if (strstr(t, "function_call"))
        return SEV_FUNCTION_CALL;
    if (strstr(t, "reasoning"))
        return SEV_REASONING;
    if (strstr(t, "image"))
        return SEV_IMAGE_GEN;
    if (strcmp(t, "done") == 0 || strstr(t, "completed"))
        return SEV_DONE;
    if (strstr(t, "error"))
        return SEV_ERROR;
    return SEV_UNKNOWN;
}

int stream_event_process(stream_event_ctx_t *ctx, const cJSON *event) {
    if (!ctx || !event)
        return -1;

    stream_event_kind_t kind = classify_event(event);

    switch (kind) {
    case SEV_CONTENT_DELTA: {
        const char *delta = nc_cjson_str(event, "delta", NULL);
        if (delta)
            strbuf_append_str(&ctx->content_accum, delta);
        break;
    }
    case SEV_TOOL_CALL_DELTA: {
        if (ctx->count >= SEV_MAX_DELTAS)
            break;
        stream_delta_t *d = &ctx->deltas[ctx->count];
        memset(d, 0, sizeof(*d));
        d->kind = kind;
        d->call_id = nc_cjson_dup_str(event, "call_id", NULL);
        d->name = nc_cjson_dup_str(event, "name", NULL);
        d->content = nc_cjson_dup_str(event, "arguments", NULL);
        ctx->count++;
        break;
    }
    case SEV_TOOL_CALL_DONE:
        ctx->tool_call_count++;
        break;
    case SEV_REASONING: {
        const char *text = nc_cjson_str(event, "text", NULL);
        if (text)
            strbuf_append_str(&ctx->reasoning_accum, text);
        break;
    }
    case SEV_DONE: {
        ctx->done = true;
        ctx->finish_reason = nc_cjson_dup_str(event, "finish_reason", NULL);
        break;
    }
    case SEV_ERROR:
        ctx->error = true;
        break;
    default:
        break;
    }
    return (int)kind;
}

static char *sanitize_id(const char *id) {
    if (!id)
        return nc_xstrdup("generated");
    size_t len = strlen(id);
    char *out = malloc(len + 1);
    if (!out)
        return nc_xstrdup("generated");
    for (size_t i = 0; i < len; i++)
        out[i] = (id[i] >= 'a' && id[i] <= 'z') || (id[i] >= 'A' && id[i] <= 'Z') ||
                         (id[i] >= '0' && id[i] <= '9') || id[i] == '-' || id[i] == '_'
                     ? id[i]
                     : '_';
    out[len] = '\0';
    return out;
}

char *stream_event_image_path(const char *home, const char *session_id, const char *call_id) {
    if (!home)
        return NULL;
    char *s_sid = sanitize_id(session_id);
    char *s_cid = sanitize_id(call_id);
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/%s/%s.png", home, SEV_IMG_ARTIFACT_DIR, s_sid, s_cid);
    free(s_sid);
    free(s_cid);
    return nc_xstrdup(path);
}

const char *stream_event_get_content(const stream_event_ctx_t *ctx) {
    return ctx ? ctx->content_accum.data : NULL;
}

const char *stream_event_get_reasoning(const stream_event_ctx_t *ctx) {
    return ctx ? ctx->reasoning_accum.data : NULL;
}

bool stream_event_is_done(const stream_event_ctx_t *ctx) {
    return ctx && ctx->done;
}

int stream_event_tool_count(const stream_event_ctx_t *ctx) {
    return ctx ? ctx->tool_call_count : 0;
}
