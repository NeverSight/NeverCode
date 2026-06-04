/*
 * request_perms.c — RequestPermissions tool
 *
 * Allows the model to request additional permissions at runtime.
 * User is prompted to approve or deny each requested permission.
 */

#include "nevercode.h"
#include "permission.h"
#include "terminal.h"
#include "tools.h"
#include "cJSON.h"

void tool_request_perms_register(void) {
    tool_def_t t = {0};
    t.name = "RequestPermissions";
    t.description = "Request additional permissions at runtime. "
                    "Use when a task requires write access or network access "
                    "that was not initially granted.";
    t.input_schema =
        cJSON_Parse("{\"type\":\"object\",\"properties\":{"
                    "\"permissions\":{\"type\":\"array\",\"items\":{\"type\":\"object\","
                    "\"properties\":{\"tool\":{\"type\":\"string\"},"
                    "\"path\":{\"type\":\"string\"}},\"required\":[\"tool\"]}},"
                    "\"reason\":{\"type\":\"string\"}},\"required\":[\"permissions\"]}");
    t.execute = request_perms_execute;
    t.read_only = false;
    t.concurrency_safe = false;
    t.destructive = false;
    tools_register(&t);
}

int request_perms_execute(const cJSON *input, char **output, void *ctx) {
    if (!input || !output) {
        *output = nc_xstrdup("Error: missing input");
        return 1;
    }

    const cJSON *permissions = cJSON_GetObjectItem(input, "permissions");
    if (!permissions || !cJSON_IsArray(permissions)) {
        *output = nc_xstrdup("Error: 'permissions' must be a JSON array");
        return 1;
    }

    int count = cJSON_GetArraySize(permissions);
    if (count == 0) {
        *output = nc_xstrdup("Error: at least one permission required");
        return 1;
    }

    const cJSON *reason = cJSON_GetObjectItem(input, "reason");
    const char *reason_str = (reason && cJSON_IsString(reason)) ? reason->valuestring : NULL;

    strbuf_t sb;
    strbuf_init(&sb);

    cJSON *results = cJSON_CreateArray();
    int approved_count = 0;
    int denied_count = 0;

    for (int i = 0; i < count; i++) {
        cJSON *perm = cJSON_GetArrayItem(permissions, i);
        if (!cJSON_IsObject(perm))
            continue;

        const cJSON *tool_j = cJSON_GetObjectItem(perm, "tool");
        const cJSON *path_j = cJSON_GetObjectItem(perm, "path");

        const char *tool = (tool_j && cJSON_IsString(tool_j)) ? tool_j->valuestring : NULL;
        const char *path = (path_j && cJSON_IsString(path_j)) ? path_j->valuestring : NULL;

        if (!tool)
            continue;

        char detail[NC_MSG_MAX];
        if (reason_str) {
            snprintf(detail, sizeof(detail), "Tool: %s%s%s\nReason: %s", tool,
                     path ? ", Path: " : "", path ? path : "", reason_str);
        } else {
            snprintf(detail, sizeof(detail), "Tool: %s%s%s", tool, path ? ", Path: " : "",
                     path ? path : "");
        }

        fprintf(stdout, "\n" T_YELLOW "⚠ Permission request:" T_RESET " %s\n", detail);

        int user_choice = perm_prompt_user(tool, detail);

        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "tool", tool);
        if (path)
            cJSON_AddStringToObject(result, "path", path);

        if (user_choice > 0) {
            cJSON_AddStringToObject(result, "status", "approved");
            approved_count++;

            perm_ctx_t *pctx = (perm_ctx_t *)ctx;
            if (pctx && user_choice == 2) {
                perm_add_rule(pctx, tool, path, PERM_ALLOW);
            }
        } else {
            cJSON_AddStringToObject(result, "status", "denied");
            denied_count++;
        }
        cJSON_AddItemToArray(results, result);
    }

    char *json_out = nc_cjson_detach(results);

    char summary[NC_LABEL_MAX];
    snprintf(summary, sizeof(summary), "Permission request: %d approved, %d denied\n%s",
             approved_count, denied_count, json_out);
    free(json_out);

    *output = nc_xstrdup(summary);
    return 0;
}
