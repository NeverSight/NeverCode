#include "tools.h"
#include "nevercode.h"

static const char *SCHEMA = "{"
                            "\"type\":\"object\","
                            "\"properties\":{"
                            "\"seconds\":{\"type\":\"integer\",\"description\":"
                            "\"Number of seconds to wait (1-300)\"},"
                            "\"reason\":{\"type\":\"string\",\"description\":"
                            "\"Why the delay is needed (for logging)\"}"
                            "},"
                            "\"required\":[\"seconds\"]"
                            "}";

static int sleep_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    cJSON *sec_j = cJSON_GetObjectItem((cJSON *)input, "seconds");
    if (!sec_j || !cJSON_IsNumber(sec_j)) {
        *output = nc_xstrdup("error: missing 'seconds'");
        return 1;
    }

    int seconds = (int)sec_j->valuedouble;
    if (seconds < 1)
        seconds = 1;
    if (seconds > SLEEP_TOOL_MAX_SECONDS)
        seconds = SLEEP_TOOL_MAX_SECONDS;

    const char *reason = nc_cjson_str((cJSON *)input, "reason", NULL);

    sleep((unsigned int)seconds);

    char buf[NC_LABEL_MAX];
    if (reason)
        snprintf(buf, sizeof(buf), "Waited %d second(s). Reason: %s", seconds, reason);
    else
        snprintf(buf, sizeof(buf), "Waited %d second(s).", seconds);

    *output = nc_xstrdup(buf);
    return 0;
}

static tool_def_t sleep_tool;

void tool_sleep_register(void) {
    sleep_tool.name = "Sleep";
    sleep_tool.description = "Wait for a specified number of seconds. Use when you need to wait "
                             "for an operation to complete, a process to start, or a resource to "
                             "become available.";
    sleep_tool.input_schema = cJSON_Parse(SCHEMA);
    sleep_tool.execute = sleep_exec;
    sleep_tool.read_only = true;
    sleep_tool.concurrency_safe = true;
    sleep_tool.destructive = false;
    tools_register(&sleep_tool);
}
