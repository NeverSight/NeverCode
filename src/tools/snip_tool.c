/*
 * snip_tool.c — History message pruning.
 *
 * Allows the agent to explicitly remove a range of messages from
 * the conversation history to free context window space. Unlike
 * compaction (which summarizes), snip directly deletes specified
 * message indices — useful when old tool results are no longer relevant.
 */

#include "nevercode.h"
#include "defaults.h"
#include "tools.h"

/* NC_NC_SNIP_MAX_RANGE in defaults.h */

static int snip_execute(const cJSON *input, char **output, void *ctx) {
    (void)ctx;

    cJSON *jfrom = cJSON_GetObjectItem(input, "from_index");
    cJSON *jto = cJSON_GetObjectItem(input, "to_index");

    if (!jfrom || !cJSON_IsNumber(jfrom) || !jto || !cJSON_IsNumber(jto)) {
        if (output)
            *output = nc_xstrdup("Error: from_index and to_index are required integer parameters");
        return -1;
    }

    int from = jfrom->valueint;
    int to = jto->valueint;

    if (from < 0 || to < from) {
        if (output)
            *output = nc_xstrdup("Error: invalid range (from must be >= 0, to must be >= from)");
        return -1;
    }

    if (to - from + 1 > NC_SNIP_MAX_RANGE) {
        if (output) {
            strbuf_t sb;
            strbuf_init(&sb);
            strbuf_appendf(&sb, "Error: range too large (max %d messages per snip)",
                           NC_SNIP_MAX_RANGE);
            *output = strbuf_detach(&sb);
        }
        return -1;
    }

    int snipped = to - from + 1;

    cJSON *jreason = cJSON_GetObjectItem(input, "reason");
    const char *reason = (jreason && cJSON_IsString(jreason)) ? jreason->valuestring : NULL;

    if (output) {
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_appendf(&sb, "Snipped %d message(s) [%d..%d]", snipped, from, to);
        if (reason)
            strbuf_appendf(&sb, " — %s", reason);
        strbuf_append_str(&sb,
                          "\n\nNote: The actual removal will be applied by the context manager "
                          "before the next API call.");
        *output = strbuf_detach(&sb);
    }
    return 0;
}

void snip_register_tool(void) {
    tool_def_t def = {0};
    def.name = "Snip";
    def.description = "Remove a range of messages from conversation history to free context "
                      "window space. Use when old tool results are irrelevant and consuming "
                      "too many tokens. Prefer this over letting auto-compact summarize data "
                      "that can simply be discarded.";
    def.execute = snip_execute;
    def.read_only = true;

    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *props = cJSON_AddObjectToObject(schema, "properties");

    cJSON *pf = cJSON_AddObjectToObject(props, "from_index");
    cJSON_AddStringToObject(pf, "type", "integer");
    cJSON_AddStringToObject(pf, "description",
                            "Start index of the message range to remove (0-based, inclusive)");

    cJSON *pt = cJSON_AddObjectToObject(props, "to_index");
    cJSON_AddStringToObject(pt, "type", "integer");
    cJSON_AddStringToObject(pt, "description",
                            "End index of the message range to remove (inclusive)");

    cJSON *pr = cJSON_AddObjectToObject(props, "reason");
    cJSON_AddStringToObject(pr, "type", "string");
    cJSON_AddStringToObject(pr, "description", "Brief reason for the snip (logged for debugging)");

    cJSON *req = cJSON_CreateArray();
    cJSON_AddItemToArray(req, cJSON_CreateString("from_index"));
    cJSON_AddItemToArray(req, cJSON_CreateString("to_index"));
    cJSON_AddItemToObject(schema, "required", req);

    def.input_schema = schema;
    tools_register(&def);
}

void tool_snip_register(void) {
    snip_register_tool();
}
