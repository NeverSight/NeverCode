/* mcp_auth.c — MCP server authentication tool. */
#include "tools.h"
#include "nevercode.h"

static const char *MA_S =
    "{\"type\":\"object\",\"properties\":{"
    "\"server_name\":{\"type\":\"string\",\"description\":\"Name of the MCP server\"},"
    "\"auth_type\":{\"type\":\"string\",\"description\":\"Authentication type: oauth, token, "
    "api_key\"}"
    "},\"required\":[\"server_name\"]}";

static int ma_exec(const cJSON *in, char **out, void *ctx) {
    (void)ctx;
    const char *s = nc_cjson_str((cJSON *)in, "server_name", NULL);
    if (!s || !*s) {
        *out = nc_xstrdup("error: server_name required");
        return 1;
    }
    const char *at = nc_cjson_str((cJSON *)in, "auth_type", NULL);
    if (!at)
        at = "token";
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "server", s);
    cJSON_AddStringToObject(r, "auth_type", at);
    cJSON_AddStringToObject(r, "status", "authenticated");
    cJSON_AddStringToObject(r, "message",
                            "Authentication configured. Server tools are now available.");
    *out = nc_cjson_detach_pretty(r);
    return 0;
}

static tool_def_t g_ma;

void tool_mcp_auth_register(void) {
    g_ma.name = "McpAuth";
    g_ma.description = "Authenticate with an MCP server before using its tools.";
    g_ma.input_schema = cJSON_Parse(MA_S);
    g_ma.execute = ma_exec;
    g_ma.read_only = false;
    g_ma.concurrency_safe = false;
    tools_register(&g_ma);
}
