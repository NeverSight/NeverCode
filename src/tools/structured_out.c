/* structured_out.c — Structured output tool for non-interactive / SDK mode. */
#include "tools.h"
#include "nevercode.h"

static const char *SO_S =
    "{\"type\":\"object\","
    "\"properties\":{\"payload\":{\"type\":\"string\",\"description\":\"JSON-encoded structured "
    "output\"}},"
    "\"required\":[\"payload\"],"
    "\"additionalProperties\":true,"
    "\"description\":\"Structured output payload matching the requested schema\"}";

static int so_exec(const cJSON *in, char **out, void *ctx) {
    (void)ctx;
    if (!in) {
        *out = nc_xstrdup("error: empty input");
        return 1;
    }
    char *r = cJSON_Print((cJSON *)in);
    if (!r) {
        *out = nc_xstrdup("error: serialize failed");
        return 1;
    }
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "status", "ok");
    cJSON_AddRawToObject(res, "structured_output", r);
    *out = nc_cjson_detach_pretty(res);
    free(r);
    return 0;
}

static tool_def_t g_so;

void tool_structured_out_register(void) {
    g_so.name = "StructuredOutput";
    g_so.description = "Return structured output in the requested format.";
    g_so.input_schema = cJSON_Parse(SO_S);
    g_so.execute = so_exec;
    g_so.read_only = true;
    g_so.concurrency_safe = true;
    tools_register(&g_so);
}
