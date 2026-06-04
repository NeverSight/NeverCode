/*
 * ctx_normalize.c — Context normalization module
 *
 * Ensures conversation items are well-formed before sending to the model:
 * - Missing tool call outputs get synthetic "aborted" entries
 * - Hidden markup (citations, plan tags) is stripped from assistant text
 * - Image placeholders are inserted when the model lacks vision
 */

#include "repl.h"
#include "api.h"

typedef struct {
    char *call_id;
    int index;
} pending_call_t;

static bool has_output_for_call(const cJSON *items, const char *call_id) {
    const cJSON *it;
    cJSON_ArrayForEach(it, items) {
        const cJSON *role = cJSON_GetObjectItem(it, "role");
        const cJSON *cid = cJSON_GetObjectItem(it, "call_id");
        if (role && cJSON_IsString(role) && strcmp(role->valuestring, "tool") == 0 && cid &&
            cJSON_IsString(cid) && strcmp(cid->valuestring, call_id) == 0)
            return true;
    }
    return false;
}

int ctx_normalize_ensure_outputs(cJSON *items) {
    if (!items || !cJSON_IsArray(items))
        return 0;

    pending_call_t pending[NEVERCODE_MAX_TOOL_CALLS];
    int npending = 0;

    int idx = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, items) {
        const cJSON *tc = cJSON_GetObjectItem(it, "tool_calls");

        if (tc && cJSON_IsArray(tc)) {
            const cJSON *call;
            cJSON_ArrayForEach(call, tc) {
                const cJSON *id = cJSON_GetObjectItem(call, "id");
                if (id && cJSON_IsString(id) && !has_output_for_call(items, id->valuestring)) {
                    if (npending < NEVERCODE_MAX_TOOL_CALLS) {
                        pending[npending].call_id = nc_xstrdup(id->valuestring);
                        pending[npending].index = idx;
                        npending++;
                    }
                }
            }
        }
        idx++;
    }

    for (int i = npending - 1; i >= 0; i--) {
        cJSON *syn = cJSON_CreateObject();
        cJSON_AddStringToObject(syn, "role", "tool");
        cJSON_AddStringToObject(syn, "call_id", pending[i].call_id);
        cJSON_AddStringToObject(syn, "content", CTX_NORM_ABORTED_TEXT);

        int insert_pos = pending[i].index + 1;
        int arr_sz = cJSON_GetArraySize(items);
        if (insert_pos >= arr_sz)
            cJSON_AddItemToArray(items, syn);
        else
            cJSON_InsertItemInArray(items, insert_pos, syn);

        free(pending[i].call_id);
    }

    return npending;
}

static char *strip_between(const char *text, const char *open, const char *close) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *p = text;
    size_t olen = strlen(open), clen = strlen(close);

    while (*p) {
        const char *start = strstr(p, open);
        if (!start) {
            strbuf_append_str(&sb, p);
            break;
        }
        strbuf_append(&sb, p, (size_t)(start - p));
        const char *end = strstr(start + olen, close);
        if (!end) {
            strbuf_append_str(&sb, start);
            break;
        }
        p = end + clen;
    }
    return strbuf_detach(&sb);
}

char *ctx_normalize_strip_citations(const char *text) {
    if (!text)
        return NULL;
    return strip_between(text, CTX_NORM_CITATION_OPEN, CTX_NORM_CITATION_CLOSE);
}

char *ctx_normalize_strip_plan_tags(const char *text) {
    if (!text)
        return NULL;
    return strip_between(text, CTX_NORM_PLAN_OPEN, CTX_NORM_PLAN_CLOSE);
}

char *ctx_normalize_strip_markup(const char *text, bool in_plan_mode) {
    if (!text)
        return NULL;
    char *no_cite = ctx_normalize_strip_citations(text);
    if (in_plan_mode) {
        char *no_plan = ctx_normalize_strip_plan_tags(no_cite);
        free(no_cite);
        return no_plan;
    }
    return no_cite;
}

int ctx_normalize_replace_images(cJSON *items, bool model_supports_vision) {
    if (!items || !cJSON_IsArray(items) || model_supports_vision)
        return 0;

    int replaced = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, items) {
        cJSON *content = cJSON_GetObjectItem(it, "content");
        if (!content || !cJSON_IsArray(content))
            continue;

        cJSON *part;
        cJSON_ArrayForEach(part, content) {
            cJSON *type = cJSON_GetObjectItem(part, "type");
            if (type && cJSON_IsString(type) && strcmp(type->valuestring, "image_url") == 0) {
                cJSON_ReplaceItemInObject(part, "type", cJSON_CreateString("text"));
                cJSON_DeleteItemFromObject(part, "image_url");
                cJSON_AddStringToObject(part, "text", CTX_NORM_IMG_OMIT);
                replaced++;
            }
        }
    }
    return replaced;
}

int ctx_normalize_remove_orphan_outputs(cJSON *items) {
    if (!items || !cJSON_IsArray(items))
        return 0;

    int call_count = 0;
    char *call_ids[NEVERCODE_MAX_TOOL_CALLS * 4];
    const cJSON *it;
    cJSON_ArrayForEach(it, items) {
        const cJSON *tc = cJSON_GetObjectItem(it, "tool_calls");
        if (!tc || !cJSON_IsArray(tc))
            continue;
        const cJSON *call;
        cJSON_ArrayForEach(call, tc) {
            const cJSON *id = cJSON_GetObjectItem(call, "id");
            if (id && cJSON_IsString(id) && call_count < NC_ARRAY_LEN(call_ids))
                call_ids[call_count++] = id->valuestring;
        }
    }

    int removed = 0;
    int sz = cJSON_GetArraySize(items);
    for (int i = sz - 1; i >= 0; i--) {
        cJSON *msg = cJSON_GetArrayItem(items, i);
        const cJSON *role = cJSON_GetObjectItem(msg, "role");
        const cJSON *cid = cJSON_GetObjectItem(msg, "call_id");
        if (!role || !cJSON_IsString(role) || strcmp(role->valuestring, "tool") != 0)
            continue;
        if (!cid || !cJSON_IsString(cid))
            continue;
        bool found = false;
        for (int j = 0; j < call_count; j++) {
            if (strcmp(call_ids[j], cid->valuestring) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            cJSON_DeleteItemFromArray(items, i);
            removed++;
        }
    }
    return removed;
}

int ctx_normalize_all(cJSON *items, bool plan_mode, bool has_vision) {
    int fixes = 0;
    fixes += ctx_normalize_remove_orphan_outputs(items);
    fixes += ctx_normalize_ensure_outputs(items);
    fixes += ctx_normalize_replace_images(items, has_vision);

    cJSON *it;
    cJSON_ArrayForEach(it, items) {
        cJSON *role = cJSON_GetObjectItem(it, "role");
        if (!role || !cJSON_IsString(role) || strcmp(role->valuestring, "assistant") != 0)
            continue;

        cJSON *content = cJSON_GetObjectItem(it, "content");
        if (content && cJSON_IsString(content)) {
            char *clean = ctx_normalize_strip_markup(content->valuestring, plan_mode);
            if (clean && strcmp(clean, content->valuestring) != 0) {
                cJSON_SetValuestring(content, clean);
                fixes++;
            }
            free(clean);
        }
    }
    return fixes;
}
