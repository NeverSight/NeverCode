/*
 * tool_error.c — Structured tool error handling
 *
 * Provides a unified error type for tool invocations with
 * categorization, retry guidance, and user-facing messages.
 */

#include "nevercode.h"
#include <stdarg.h>

/* Macros from defaults.h: TERR_MSG_MAX, TERR_DETAIL_MAX */

typedef enum {
    TERR_NONE,
    TERR_INVALID_INPUT,
    TERR_NOT_FOUND,
    TERR_PERMISSION,
    TERR_TIMEOUT,
    TERR_EXEC_FAILED,
    TERR_OUTPUT_TOO_LARGE,
    TERR_RESOURCE_BUSY,
    TERR_NETWORK,
    TERR_INTERNAL,
    TERR_SANDBOX,
    TERR_CANCELLED,
    TERR_UNKNOWN
} tool_error_kind_t;

typedef struct {
    tool_error_kind_t kind;
    char tool_name[NC_FMT_MAX];
    char message[TERR_MSG_MAX];
    char detail[TERR_DETAIL_MAX];
    bool retryable;
    int exit_code;
} tool_error_t;

static const char *terr_kind_names[] = {
    "none",        "invalid_input",    "not_found",     "permission", "timeout",
    "exec_failed", "output_too_large", "resource_busy", "network",    "internal",
    "sandbox",     "cancelled",        "unknown"};

void terr_init(tool_error_t *err) {
    memset(err, 0, sizeof(*err));
    err->kind = TERR_NONE;
    err->exit_code = -1;
}

void terr_set(tool_error_t *err, tool_error_kind_t kind, const char *tool, const char *message) {
    if (!err)
        return;
    err->kind = kind;
    if (tool)
        nc_strlcpy(err->tool_name, tool, sizeof(err->tool_name));
    if (message)
        nc_strlcpy(err->message, message, sizeof(err->message));

    switch (kind) {
    case TERR_TIMEOUT:
    case TERR_RESOURCE_BUSY:
    case TERR_NETWORK:
        err->retryable = true;
        break;
    default:
        err->retryable = false;
        break;
    }
}

void terr_set_detail(tool_error_t *err, const char *fmt, ...) {
    if (!err || !fmt)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->detail, sizeof(err->detail), fmt, ap);
    va_end(ap);
}

const char *terr_kind_name(tool_error_kind_t kind) {
    if (kind < 0 || kind >= NC_ARRAY_LEN(terr_kind_names))
        return "unknown";
    return terr_kind_names[kind];
}

char *terr_format(const tool_error_t *err) {
    if (!err || err->kind == TERR_NONE)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Tool error [%s] in %s: %s", terr_kind_name(err->kind),
                   err->tool_name[0] ? err->tool_name : "unknown",
                   err->message[0] ? err->message : "(no message)");
    if (err->detail[0])
        strbuf_appendf(&sb, "\nDetail: %s", err->detail);
    if (err->retryable)
        strbuf_append_str(&sb, "\n(retryable)");
    if (err->exit_code >= 0)
        strbuf_appendf(&sb, "\nexit_code=%d", err->exit_code);
    return strbuf_detach(&sb);
}

char *terr_to_json(const tool_error_t *err) {
    if (!err || err->kind == TERR_NONE)
        return NULL;

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "kind", terr_kind_name(err->kind));
    cJSON_AddStringToObject(obj, "tool", err->tool_name);
    cJSON_AddStringToObject(obj, "message", err->message);
    if (err->detail[0])
        cJSON_AddStringToObject(obj, "detail", err->detail);
    cJSON_AddBoolToObject(obj, "retryable", err->retryable);
    if (err->exit_code >= 0)
        cJSON_AddNumberToObject(obj, "exit_code", err->exit_code);

    char *json = nc_cjson_detach(obj);
    return json;
}

tool_error_kind_t terr_from_exit_code(int exit_code) {
    if (exit_code == 0)
        return TERR_NONE;
    if (exit_code == NC_EXIT_TIMEOUT || exit_code == NC_EXIT_SIGNAL(SIGKILL))
        return TERR_TIMEOUT;
    if (exit_code == 126)
        return TERR_PERMISSION;
    if (exit_code == 127)
        return TERR_NOT_FOUND;
    return TERR_EXEC_FAILED;
}

bool terr_is_retryable(const tool_error_t *err) {
    return err && err->retryable;
}

bool terr_has_error(const tool_error_t *err) {
    return err && err->kind != TERR_NONE;
}
