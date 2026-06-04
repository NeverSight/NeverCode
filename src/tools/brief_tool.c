#include "tools.h"
#include "repl.h"

static const char *SCHEMA = "{"
                            "\"type\":\"object\","
                            "\"properties\":{"
                            "\"message\":{\"type\":\"string\","
                            "\"description\":\"The concise message to send to the user\"}"
                            "},"
                            "\"required\":[\"message\"]"
                            "}";

static int brief_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *message = nc_cjson_str((cJSON *)input, "message", NULL);
    if (!message) {
        *output = nc_xstrdup("error: missing 'message' parameter");
        return 1;
    }

    g_brief_mode = true;

    *output = nc_xstrdup(message);
    return 0;
}

static tool_def_t brief_tool;

void tool_brief_register(void) {
    brief_tool.name = "Brief";
    brief_tool.description =
        "Send a concise message to the user. Use this when you want to "
        "communicate a key result or status update without verbose explanation. "
        "The message should be focused and actionable.";
    brief_tool.input_schema = cJSON_Parse(SCHEMA);
    brief_tool.execute = brief_exec;
    brief_tool.read_only = true;
    brief_tool.concurrency_safe = true;
    brief_tool.destructive = false;
    tools_register(&brief_tool);
}
