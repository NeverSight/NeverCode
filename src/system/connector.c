/*
 * connector.c — JSON-RPC 2.0 method dispatcher for IDE integration.
 *
 * Registers named methods with handler callbacks. Dispatches incoming
 * JSON-RPC requests, returns properly formatted responses. Includes
 * a stdio transport mode for piped communication.
 */

#include "nevercode.h"

typedef struct {
    char method[CONNECTOR_METHOD_MAX];
    connector_handler_fn fn;
    void *ctx;
} handler_entry_t;

static struct {
    handler_entry_t handlers[CONNECTOR_MAX_HANDLERS];
    int count;
    bool running;
} g;

void connector_init(void) {
    memset(&g, 0, sizeof g);
}
void connector_free(void) {
    memset(&g, 0, sizeof g);
}

int connector_register(const char *method, connector_handler_fn fn, void *ctx) {
    if (!method || !fn || g.count >= CONNECTOR_MAX_HANDLERS)
        return -1;
    handler_entry_t *e = &g.handlers[g.count];
    nc_strlcpy(e->method, method, sizeof e->method);
    e->fn = fn;
    e->ctx = ctx;
    g.count++;
    return 0;
}

static connector_handler_fn find_handler(const char *method, void **ctx) {
    for (int i = 0; i < g.count; i++) {
        if (strcmp(g.handlers[i].method, method) == 0) {
            *ctx = g.handlers[i].ctx;
            return g.handlers[i].fn;
        }
    }
    return NULL;
}

static char *make_response(const cJSON *id, cJSON *result, const char *error_msg, int error_code) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");

    if (id)
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
    else
        cJSON_AddNullToObject(resp, "id");

    if (error_msg) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddNumberToObject(err, "code", error_code);
        cJSON_AddStringToObject(err, "message", error_msg);
        cJSON_AddItemToObject(resp, "error", err);
    } else {
        if (result)
            cJSON_AddItemToObject(resp, "result", cJSON_Duplicate(result, 1));
        else
            cJSON_AddNullToObject(resp, "result");
    }

    return nc_cjson_detach(resp);
}

int connector_dispatch(const char *json_line, char **response) {
    if (!json_line || !response)
        return -1;
    *response = NULL;

    cJSON *req = cJSON_Parse(json_line);
    if (!req) {
        *response = make_response(NULL, NULL, "Parse error", -32700);
        return -1;
    }

    const cJSON *method_j = cJSON_GetObjectItemCaseSensitive(req, "method");
    const cJSON *params_j = cJSON_GetObjectItemCaseSensitive(req, "params");
    const cJSON *id_j = cJSON_GetObjectItemCaseSensitive(req, "id");

    if (!cJSON_IsString(method_j) || !method_j->valuestring) {
        *response = make_response(id_j, NULL, "Invalid method", -32600);
        cJSON_Delete(req);
        return -1;
    }

    void *hctx = NULL;
    connector_handler_fn fn = find_handler(method_j->valuestring, &hctx);
    if (!fn) {
        *response = make_response(id_j, NULL, "Method not found", -32601);
        cJSON_Delete(req);
        return -1;
    }

    cJSON *result = NULL;
    int rc = fn(params_j, &result, hctx);
    if (rc != 0) {
        char msg[NC_TAG_MAX];
        snprintf(msg, sizeof msg, "Handler error (rc=%d)", rc);
        *response = make_response(id_j, NULL, msg, -32000);
    } else {
        *response = make_response(id_j, result, NULL, 0);
    }

    if (result)
        cJSON_Delete(result);
    cJSON_Delete(req);
    return rc;
}

char *connector_format_methods(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Connector: %d methods registered\n", g.count);
    for (int i = 0; i < g.count; i++)
        strbuf_appendf(&sb, "  %s\n", g.handlers[i].method);
    return strbuf_detach(&sb);
}
