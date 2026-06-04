#include "tools.h"
#include "nevercode.h"

static const char *SCHEMA = "{"
                            "\"type\":\"object\","
                            "\"properties\":{"
                            "\"query\":{\"type\":\"string\",\"description\":"
                            "\"Search keyword to match against tool names and descriptions\"}"
                            "},"
                            "\"required\":[\"query\"]"
                            "}";

static int tool_search_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *query = nc_cjson_str((cJSON *)input, "query", NULL);
    if (!query || !*query) {
        *output = nc_xstrdup("error: missing 'query'");
        return 1;
    }

    int nt = tools_count();
    const tool_def_t *all = tools_get_all();
    if (!all || nt == 0) {
        *output = nc_xstrdup("No tools registered.");
        return 0;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    int matches = 0;

    for (int i = 0; i < nt; i++) {
        bool hit = nc_contains_ci(all[i].name, query) || nc_contains_ci(all[i].description, query);
        if (!hit)
            continue;

        matches++;
        strbuf_append_str(&sb, "- **");
        strbuf_append_str(&sb, all[i].name);
        strbuf_append_str(&sb, "**");
        if (all[i].read_only)
            strbuf_append_str(&sb, " [read-only]");
        if (all[i].concurrency_safe)
            strbuf_append_str(&sb, " [parallel-safe]");
        strbuf_append_str(&sb, ": ");
        if (all[i].description)
            strbuf_append_str(&sb, all[i].description);

        if (all[i].input_schema) {
            cJSON *props = cJSON_GetObjectItem(all[i].input_schema, "properties");
            cJSON *req = cJSON_GetObjectItem(all[i].input_schema, "required");
            if (props && cJSON_GetArraySize(props) > 0) {
                strbuf_append_str(&sb, "\n  Params: ");
                cJSON *p = NULL;
                int first = 1;
                cJSON_ArrayForEach(p, props) {
                    if (!first)
                        strbuf_append_str(&sb, ", ");
                    first = 0;
                    strbuf_append_str(&sb, p->string);
                    const char *ptype = nc_cjson_str(p, "type", NULL);
                    if (ptype) {
                        strbuf_append_str(&sb, "(");
                        strbuf_append_str(&sb, ptype);
                        strbuf_append_str(&sb, ")");
                    }
                    bool is_req = false;
                    if (req) {
                        int nr = cJSON_GetArraySize(req);
                        for (int r = 0; r < nr; r++) {
                            const char *rv = cJSON_GetStringValue(cJSON_GetArrayItem(req, r));
                            if (rv && strcmp(rv, p->string) == 0) {
                                is_req = true;
                                break;
                            }
                        }
                    }
                    if (is_req)
                        strbuf_append_str(&sb, "*");
                }
            }
        }
        strbuf_append_str(&sb, "\n");
    }

    if (matches == 0) {
        strbuf_free(&sb);
        char msg[NC_LABEL_MAX];
        snprintf(msg, sizeof(msg),
                 "No tools matching '%s'. "
                 "Available: %d tools total.",
                 query, nt);
        *output = nc_xstrdup(msg);
        return 0;
    }

    char header[NC_TAG_MAX];
    snprintf(header, sizeof(header), "Found %d tool(s) matching '%s':\n", matches, query);
    strbuf_t result;
    strbuf_init(&result);
    strbuf_append_str(&result, header);
    strbuf_append_str(&result, sb.data);
    strbuf_free(&sb);
    *output = strbuf_detach(&result);
    return 0;
}

static tool_def_t tool_search_tool;

void tool_tool_search_register(void) {
    tool_search_tool.name = "ToolSearch";
    tool_search_tool.description = "Search for available tools by keyword. "
                                   "Useful when many tools are registered (especially with MCP).";
    tool_search_tool.input_schema = cJSON_Parse(SCHEMA);
    tool_search_tool.execute = tool_search_exec;
    tool_search_tool.read_only = true;
    tool_search_tool.concurrency_safe = true;
    tool_search_tool.destructive = false;
    tools_register(&tool_search_tool);
}
