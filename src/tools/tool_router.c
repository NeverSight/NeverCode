/*
 * tool_router.c — Tool call classification, routing, and dispatch.
 *
 * Classifies incoming tool calls by payload kind (function, shell,
 * MCP, custom, search) and routes them to the appropriate executor.
 * Tracks per-kind statistics and supports parallel execution queries.
 */

#include "nevercode.h"
#include "tools.h"

static struct {
    bool initialized;
    char *mcp_prefixes[ROUTER_MAX_MCP_PREFIXES];
    int mcp_prefix_count;
    int total_routed;
    int by_kind[PAYLOAD_KIND_COUNT];
} g_router;

/* ── Lifecycle ── */

void tool_router_init(void) {
    memset(&g_router, 0, sizeof(g_router));
    g_router.initialized = true;
}

void tool_router_free(void) {
    for (int i = 0; i < g_router.mcp_prefix_count; i++)
        free(g_router.mcp_prefixes[i]);
    memset(&g_router, 0, sizeof(g_router));
}

int tool_router_add_mcp_prefix(const char *prefix) {
    if (!prefix || g_router.mcp_prefix_count >= ROUTER_MAX_MCP_PREFIXES)
        return -1;
    g_router.mcp_prefixes[g_router.mcp_prefix_count++] = nc_xstrdup(prefix);
    return 0;
}

/* ── Classification ── */

static bool is_mcp_tool(const char *name) {
    if (!name)
        return false;
    for (int i = 0; i < g_router.mcp_prefix_count; i++) {
        size_t plen = strlen(g_router.mcp_prefixes[i]);
        if (strncmp(name, g_router.mcp_prefixes[i], plen) == 0)
            return true;
    }
    return (strstr(name, "mcp__") == name);
}

static payload_kind_t classify_tool_call(const char *name, const cJSON *item) {
    if (!name)
        return PAYLOAD_UNKNOWN;
    if (is_mcp_tool(name))
        return PAYLOAD_MCP;
    if (strcmp(name, "local_shell") == 0 || strcmp(name, "shell") == 0 || strcmp(name, "bash") == 0)
        return PAYLOAD_LOCAL_SHELL;
    if (strcmp(name, "tool_search") == 0)
        return PAYLOAD_SEARCH;

    const tool_def_t *def = tools_find(name);
    if (def)
        return PAYLOAD_FUNCTION;

    if (item) {
        cJSON *input_node = cJSON_GetObjectItem((cJSON *)item, "input");
        if (input_node && cJSON_IsObject(input_node))
            return PAYLOAD_CUSTOM;
    }
    return PAYLOAD_FUNCTION;
}

/* ── Routed call helpers ── */

void routed_call_init(routed_call_t *rc) {
    memset(rc, 0, sizeof(*rc));
    rc->kind = PAYLOAD_UNKNOWN;
}

void routed_call_free(routed_call_t *rc) {
    free(rc->tool_name);
    free(rc->call_id);
    free(rc->namespace);
    if (rc->arguments)
        cJSON_Delete(rc->arguments);
    memset(rc, 0, sizeof(*rc));
}

/* ── Classify and route ── */

int tool_router_classify(const char *name, const cJSON *item, routed_call_t *out) {
    if (!g_router.initialized || !name || !out)
        return -1;

    routed_call_init(out);
    out->tool_name = nc_xstrdup(name);
    out->kind = classify_tool_call(name, item);

    if (item) {
        cJSON *cid = cJSON_GetObjectItem((cJSON *)item, "call_id");
        if (!cid)
            cid = cJSON_GetObjectItem((cJSON *)item, "id");
        if (cid && cJSON_IsString(cid))
            out->call_id = nc_xstrdup(cid->valuestring);

        cJSON *ns = cJSON_GetObjectItem((cJSON *)item, "namespace");
        if (ns && cJSON_IsString(ns))
            out->namespace = nc_xstrdup(ns->valuestring);

        cJSON *args = cJSON_GetObjectItem((cJSON *)item, "arguments");
        if (!args)
            args = cJSON_GetObjectItem((cJSON *)item, "input");
        if (args)
            out->arguments = cJSON_Duplicate(args, 1);
    }

    g_router.total_routed++;
    if (out->kind >= 0 && out->kind < 6)
        g_router.by_kind[out->kind]++;
    return 0;
}

/* ── Stats ── */

int tool_router_total(void) {
    return g_router.total_routed;
}

const char *payload_kind_name(payload_kind_t k) {
    switch (k) {
    case PAYLOAD_FUNCTION:
        return "function";
    case PAYLOAD_LOCAL_SHELL:
        return "shell";
    case PAYLOAD_MCP:
        return "mcp";
    case PAYLOAD_CUSTOM:
        return "custom";
    case PAYLOAD_SEARCH:
        return "search";
    default:
        return "unknown";
    }
}

char *tool_router_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Router: total=%d", g_router.total_routed);
    for (int i = 0; i < PAYLOAD_KIND_COUNT; i++)
        if (g_router.by_kind[i] > 0)
            strbuf_appendf(&sb, " %s=%d", payload_kind_name((payload_kind_t)i),
                           g_router.by_kind[i]);
    strbuf_append_str(&sb, "\n");
    return strbuf_detach(&sb);
}
