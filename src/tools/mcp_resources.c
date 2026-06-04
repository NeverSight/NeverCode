#include "tools.h"
#include "mcp.h"
#include "repl.h"

static int list_mcp_resources_exec(const cJSON *input, char **output, void *ctx) {
    (void)input;
    (void)ctx;

    if (g_num_mcp_servers == 0) {
        *output = nc_xstrdup("No MCP servers connected.");
        return 0;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    int total = 0;

    for (int i = 0; i < g_num_mcp_servers; i++) {
        char *listing = NULL;
        int nr = mcp_list_resources(&g_mcp_servers[i], &listing);
        if (nr > 0 && listing) {
            if (sb.len > 0)
                strbuf_append_str(&sb, "\n\n");
            strbuf_append_str(&sb, "## ");
            strbuf_append_str(&sb, g_mcp_servers[i].name);
            char hdr[NC_FMT_MAX];
            snprintf(hdr, sizeof(hdr), " (%d resources)\n\n", nr);
            strbuf_append_str(&sb, hdr);
            strbuf_append_str(&sb, listing);
            total += nr;
        } else if (listing) {
            if (sb.len > 0)
                strbuf_append_str(&sb, "\n\n");
            strbuf_append_str(&sb, "## ");
            strbuf_append_str(&sb, g_mcp_servers[i].name);
            strbuf_append_str(&sb, "\n\n");
            strbuf_append_str(&sb, listing);
        }
        free(listing);
    }

    if (total == 0 && sb.len == 0)
        strbuf_append_str(&sb, "No resources available from any MCP server.");

    *output = strbuf_detach(&sb);
    return 0;
}

static int read_mcp_resource_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;

    const char *server_name = nc_cjson_str((cJSON *)input, "server", NULL);
    const char *uri = nc_cjson_str((cJSON *)input, "uri", NULL);

    if (!uri || !uri[0]) {
        *output = nc_xstrdup("error: 'uri' parameter is required");
        return 1;
    }

    if (g_num_mcp_servers == 0) {
        *output = nc_xstrdup("error: no MCP servers connected");
        return 1;
    }

    if (server_name && server_name[0]) {
        for (int i = 0; i < g_num_mcp_servers; i++) {
            if (strcmp(g_mcp_servers[i].name, server_name) == 0)
                return mcp_read_resource(&g_mcp_servers[i], uri, output);
        }
        char err[NC_LABEL_MAX];
        snprintf(err, sizeof(err), "error: MCP server '%s' not found", server_name);
        *output = nc_xstrdup(err);
        return 1;
    }

    for (int i = 0; i < g_num_mcp_servers; i++) {
        char *result = NULL;
        int rc = mcp_read_resource(&g_mcp_servers[i], uri, &result);
        if (rc == 0 && result && result[0]) {
            *output = result;
            return 0;
        }
        free(result);
    }

    char err[NC_MSG_MAX];
    snprintf(err, sizeof(err), "error: resource '%s' not found on any server", uri);
    *output = nc_xstrdup(err);
    return 1;
}

void tool_mcp_resources_register(void) {
    static cJSON *list_schema = NULL;
    if (!list_schema) {
        list_schema = cJSON_CreateObject();
        cJSON_AddStringToObject(list_schema, "type", "object");
        cJSON_AddItemToObject(list_schema, "properties", cJSON_CreateObject());
    }

    static tool_def_t list_tool = {.name = "ListMcpResources",
                                   .description =
                                       "List available resources from all connected MCP servers. "
                                       "Returns resource URIs, names, and descriptions.",
                                   .execute = list_mcp_resources_exec,
                                   .read_only = true,
                                   .concurrency_safe = true};
    list_tool.input_schema = list_schema;
    tools_register(&list_tool);

    static cJSON *read_schema = NULL;
    if (!read_schema) {
        read_schema = cJSON_CreateObject();
        cJSON_AddStringToObject(read_schema, "type", "object");
        cJSON *props = cJSON_CreateObject();

        cJSON *uri_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(uri_prop, "type", "string");
        cJSON_AddStringToObject(uri_prop, "description", "The URI of the resource to read");
        cJSON_AddItemToObject(props, "uri", uri_prop);

        cJSON *srv_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(srv_prop, "type", "string");
        cJSON_AddStringToObject(
            srv_prop, "description",
            "Optional: specific MCP server name. If omitted, tries all servers.");
        cJSON_AddItemToObject(props, "server", srv_prop);

        cJSON_AddItemToObject(read_schema, "properties", props);
        cJSON *req = cJSON_CreateArray();
        cJSON_AddItemToArray(req, cJSON_CreateString("uri"));
        cJSON_AddItemToObject(read_schema, "required", req);
    }

    static tool_def_t read_tool = {.name = "ReadMcpResource",
                                   .description =
                                       "Read the content of a specific MCP resource by URI. "
                                       "Use ListMcpResources first to discover available URIs.",
                                   .execute = read_mcp_resource_exec,
                                   .read_only = true,
                                   .concurrency_safe = true};
    read_tool.input_schema = read_schema;
    tools_register(&read_tool);
}
