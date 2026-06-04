/*
 * responses_api.c — Responses API adapter.
 *
 * Translates between the chat-completions format (internal model)
 * and the Responses API format. Handles request building, response
 * parsing, and tool call mapping.
 */

#include "nevercode.h"
#include "conversation.h"

/* RESP_API_PATH, RESP_MAX_ITEMS in defaults.h */

typedef enum {
    RESP_ITEM_MESSAGE = 0,
    RESP_ITEM_TOOL_CALL,
    RESP_ITEM_TOOL_RESULT,
    RESP_ITEM_REASONING
} resp_item_kind_t;

static struct {
    bool enabled;
    int total_requests;
    int total_errors;
    int total_tool_calls;
} g_rapi;

void responses_api_init(void) {
    memset(&g_rapi, 0, sizeof(g_rapi));
}

void responses_api_set_enabled(bool on) {
    g_rapi.enabled = on;
}

bool responses_api_enabled(void) {
    return g_rapi.enabled;
}

cJSON *responses_api_build_request(const message_t *msgs, int n, const char *system_prompt,
                                   const char *model, const tool_def_t *tools, int nt,
                                   int max_tokens) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    if (max_tokens > 0)
        cJSON_AddNumberToObject(root, "max_output_tokens", max_tokens);

    cJSON_AddStringToObject(root, "instructions", system_prompt ? system_prompt : "");

    cJSON *input = cJSON_AddArrayToObject(root, "input");
    for (int i = 0; i < n; i++) {
        const message_t *m = &msgs[i];
        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];
            cJSON *item = cJSON_CreateObject();
            bool ok = false;

            if (b->type == CONTENT_TEXT) {
                if (m->role == ROLE_USER) {
                    cJSON_AddStringToObject(item, "type", "message");
                    cJSON_AddStringToObject(item, "role", "user");
                    cJSON *content = cJSON_AddArrayToObject(item, "content");
                    cJSON *text_item = cJSON_CreateObject();
                    cJSON_AddStringToObject(text_item, "type", "input_text");
                    cJSON_AddStringToObject(text_item, "text", b->text ? b->text : "");
                    cJSON_AddItemToArray(content, text_item);
                    ok = true;
                } else if (m->role == ROLE_ASSISTANT) {
                    cJSON_AddStringToObject(item, "type", "message");
                    cJSON_AddStringToObject(item, "role", "assistant");
                    cJSON *content = cJSON_AddArrayToObject(item, "content");
                    cJSON *text_item = cJSON_CreateObject();
                    cJSON_AddStringToObject(text_item, "type", "output_text");
                    cJSON_AddStringToObject(text_item, "text", b->text ? b->text : "");
                    cJSON_AddItemToArray(content, text_item);
                    ok = true;
                }
            } else if (b->type == CONTENT_TOOL_USE) {
                cJSON_AddStringToObject(item, "type", "function_call");
                cJSON_AddStringToObject(item, "call_id", b->tool_use_id ? b->tool_use_id : "");
                cJSON_AddStringToObject(item, "name", b->tool_name ? b->tool_name : "");
                char *args_str =
                    b->tool_input ? cJSON_PrintUnformatted(b->tool_input) : nc_xstrdup("{}");
                cJSON_AddStringToObject(item, "arguments", args_str);
                free(args_str);
                ok = true;
            } else if (b->type == CONTENT_TOOL_RESULT) {
                cJSON_AddStringToObject(item, "type", "function_call_output");
                cJSON_AddStringToObject(item, "call_id", b->tool_use_id ? b->tool_use_id : "");
                cJSON_AddStringToObject(item, "output", b->text ? b->text : "");
                ok = true;
            }
            if (ok)
                cJSON_AddItemToArray(input, item);
            else
                cJSON_Delete(item);
        }
    }

    if (nt > 0) {
        cJSON *tool_arr = cJSON_AddArrayToObject(root, "tools");
        for (int i = 0; i < nt; i++) {
            cJSON *t = cJSON_CreateObject();
            cJSON_AddStringToObject(t, "type", "function");
            cJSON_AddStringToObject(t, "name", tools[i].name);
            cJSON_AddStringToObject(t, "description",
                                    tools[i].description ? tools[i].description : "");
            if (tools[i].input_schema)
                cJSON_AddItemToObject(t, "parameters", cJSON_Duplicate(tools[i].input_schema, 1));
            cJSON_AddItemToArray(tool_arr, t);
        }
        cJSON_AddBoolToObject(root, "parallel_tool_calls", 1);
    }

    g_rapi.total_requests++;
    return root;
}

bool responses_api_is_response_format(const cJSON *body) {
    return cJSON_GetObjectItem(body, "output") != NULL &&
           cJSON_GetObjectItem(body, "choices") == NULL;
}

int responses_api_parse_output(const cJSON *body, message_t *out, int max_msgs) {
    if (!body || !out || max_msgs <= 0)
        return 0;

    cJSON *output = cJSON_GetObjectItem(body, "output");
    if (output && !cJSON_IsArray(output))
        g_rapi.total_errors++;
    if (!output || !cJSON_IsArray(output))
        return 0;

    int n = 0;
    int items = cJSON_GetArraySize(output);
    for (int i = 0; i < items && n < max_msgs; i++) {
        cJSON *item = cJSON_GetArrayItem(output, i);
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(item, "type"));
        if (!type)
            continue;

        if (strcmp(type, "message") == 0) {
            const char *role_s = cJSON_GetStringValue(cJSON_GetObjectItem(item, "role"));
            cJSON *content = cJSON_GetObjectItem(item, "content");
            if (!content || !cJSON_IsArray(content))
                continue;

            message_init(&out[n],
                         (role_s && strcmp(role_s, "user") == 0) ? ROLE_USER : ROLE_ASSISTANT);

            int nc = cJSON_GetArraySize(content);
            for (int j = 0; j < nc; j++) {
                cJSON *c = cJSON_GetArrayItem(content, j);
                const char *ct = cJSON_GetStringValue(cJSON_GetObjectItem(c, "type"));
                if (!ct)
                    continue;
                if (strcmp(ct, "output_text") == 0 || strcmp(ct, "input_text") == 0) {
                    const char *txt = cJSON_GetStringValue(cJSON_GetObjectItem(c, "text"));
                    if (txt)
                        message_add_text(&out[n], txt);
                }
            }
            n++;
        } else if (strcmp(type, "function_call") == 0) {
            const char *call_id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "call_id"));
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
            const char *args_s = cJSON_GetStringValue(cJSON_GetObjectItem(item, "arguments"));

            if (n == 0 || out[n - 1].role != ROLE_ASSISTANT) {
                if (n >= max_msgs)
                    break;
                message_init(&out[n], ROLE_ASSISTANT);
                n++;
            }

            cJSON *args = args_s ? cJSON_Parse(args_s) : cJSON_CreateObject();
            message_add_tool_use(&out[n - 1], call_id ? call_id : "", name ? name : "", args);
            cJSON_Delete(args);
            g_rapi.total_tool_calls++;
        } else if (strcmp(type, "function_call_output") == 0) {
            const char *call_id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "call_id"));
            const char *output_text = cJSON_GetStringValue(cJSON_GetObjectItem(item, "output"));

            if (n >= max_msgs)
                break;
            message_init(&out[n], ROLE_TOOL);
            message_add_tool_result(&out[n], call_id ? call_id : "", output_text ? output_text : "",
                                    0);
            n++;
        }
    }

    return n;
}

const char *responses_api_finish_reason(const cJSON *body) {
    const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(body, "status"));
    if (!status)
        return NULL;
    if (strcmp(status, "completed") == 0)
        return FINISH_REASON_STOP;
    if (strcmp(status, "incomplete") == 0)
        return FINISH_REASON_LENGTH;
    if (strcmp(status, "failed") == 0)
        return "error"; /* no macro: not a standard finish_reason */
    return status;
}

int responses_api_usage(const cJSON *body, int *input_tokens, int *output_tokens) {
    cJSON *usage = cJSON_GetObjectItem(body, "usage");
    if (!usage)
        return -1;
    if (input_tokens) {
        cJSON *it = cJSON_GetObjectItem(usage, "input_tokens");
        *input_tokens = it ? it->valueint : 0;
    }
    if (output_tokens) {
        cJSON *ot = cJSON_GetObjectItem(usage, "output_tokens");
        *output_tokens = ot ? ot->valueint : 0;
    }
    return 0;
}

int responses_api_request_count(void) {
    return g_rapi.total_requests;
}

int responses_api_error_count(void) {
    return g_rapi.total_errors;
}

char *responses_api_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Responses API: enabled=%s requests=%d errors=%d tools=%d\n",
                   g_rapi.enabled ? "yes" : "no", g_rapi.total_requests, g_rapi.total_errors,
                   g_rapi.total_tool_calls);
    return strbuf_detach(&sb);
}
