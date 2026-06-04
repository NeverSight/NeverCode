/*
 * resp_debug.c — Response debug context extraction
 *
 * Extracts diagnostic metadata from HTTP error responses
 * (request IDs, CDN ray IDs, auth error codes) to aid
 * in troubleshooting API failures.
 */

#include "nevercode.h"

#define RDBG_HDR_REQUEST_ID "x-request-id"
#define RDBG_HDR_OAI_REQ_ID API_HDR_COMPAT_REQ_ID
#define RDBG_HDR_CF_RAY "cf-ray"
#define RDBG_HDR_AUTH_ERROR API_HDR_COMPAT_AUTH_ERR
#define RDBG_HDR_ERROR_JSON "x-error-json"

typedef struct {
    char *request_id;
    char *cf_ray;
    char *auth_error;
    char *error_code;
} resp_debug_t;

void resp_debug_init(resp_debug_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

void resp_debug_free(resp_debug_t *ctx) {
    if (!ctx)
        return;
    free(ctx->request_id);
    free(ctx->cf_ray);
    free(ctx->auth_error);
    free(ctx->error_code);
    memset(ctx, 0, sizeof(*ctx));
}

static char *extract_header_value(const char *headers, const char *name) {
    if (!headers || !name)
        return NULL;

    size_t nlen = strlen(name);
    const char *p = headers;

    while (*p) {
        while (*p == '\r' || *p == '\n')
            p++;
        if (!*p)
            break;

        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            p += nlen + 1;
            while (*p == ' ' || *p == '\t')
                p++;
            const char *end = p;
            while (*end && *end != '\r' && *end != '\n')
                end++;
            size_t vlen = (size_t)(end - p);
            char *val = malloc(vlen + 1);
            if (!val)
                return NULL;
            memcpy(val, p, vlen);
            val[vlen] = '\0';
            return val;
        }

        while (*p && *p != '\n')
            p++;
    }
    return NULL;
}

int resp_debug_extract(resp_debug_t *ctx, int status_code, const char *headers, const char *body) {
    (void)body;
    if (!ctx)
        return -1;
    resp_debug_free(ctx);

    ctx->request_id = extract_header_value(headers, RDBG_HDR_REQUEST_ID);
    if (!ctx->request_id)
        ctx->request_id = extract_header_value(headers, RDBG_HDR_OAI_REQ_ID);

    ctx->cf_ray = extract_header_value(headers, RDBG_HDR_CF_RAY);
    ctx->auth_error = extract_header_value(headers, RDBG_HDR_AUTH_ERROR);

    char *err_json = extract_header_value(headers, RDBG_HDR_ERROR_JSON);
    if (err_json) {
        cJSON *parsed = cJSON_Parse(err_json);
        if (parsed) {
            cJSON *err = cJSON_GetObjectItem(parsed, "error");
            if (err) {
                cJSON *code = cJSON_GetObjectItem(err, "code");
                if (code && cJSON_IsString(code))
                    ctx->error_code = nc_xstrdup(code->valuestring);
            }
            cJSON_Delete(parsed);
        }
        free(err_json);
    }

    return (ctx->request_id || ctx->cf_ray || ctx->auth_error) ? 1 : 0;
}

char *resp_debug_format(const resp_debug_t *ctx) {
    if (!ctx)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);

    if (ctx->request_id)
        strbuf_appendf(&sb, "Request-ID: %s\n", ctx->request_id);
    if (ctx->cf_ray)
        strbuf_appendf(&sb, "CF-Ray: %s\n", ctx->cf_ray);
    if (ctx->auth_error)
        strbuf_appendf(&sb, "Auth-Error: %s\n", ctx->auth_error);
    if (ctx->error_code)
        strbuf_appendf(&sb, "Error-Code: %s\n", ctx->error_code);

    return strbuf_detach(&sb);
}

bool resp_debug_is_auth_error(const resp_debug_t *ctx) {
    return ctx && ctx->auth_error != NULL;
}

bool resp_debug_has_info(const resp_debug_t *ctx) {
    return ctx && (ctx->request_id || ctx->cf_ray);
}
