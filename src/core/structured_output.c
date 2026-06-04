/*
 * structured_output.c — JSON Mode / Structured Output support
 *
 * Allows requesting the model to produce output conforming to a JSON schema.
 * Works with both response_format and tool-use-as-schema patterns.
 * The caller supplies a JSON schema; we inject it via the appropriate mechanism.
 */

#include "nevercode.h"

static const char *JSON_MODE_INSTRUCTION =
    "\n\nIMPORTANT: You MUST respond with valid JSON only. "
    "No markdown fences, no explanation text outside the JSON. "
    "Your entire response must be a single valid JSON object.\n";

cJSON *structured_output_wrap_request(const cJSON *base_body, const cJSON *schema,
                                      provider_type_t provider) {
    if (!base_body)
        return NULL;

    cJSON *body = cJSON_Duplicate(base_body, 1);

    if (provider == PROVIDER_COMPAT) {
        if (schema) {
            cJSON *rf = cJSON_CreateObject();
            cJSON_AddStringToObject(rf, "type", "json_schema");
            cJSON *js = cJSON_CreateObject();
            cJSON_AddStringToObject(js, "name", "structured_response");
            cJSON_AddBoolToObject(js, "strict", 1);
            cJSON_AddItemToObject(js, "schema", cJSON_Duplicate(schema, 1));
            cJSON_AddItemToObject(rf, "json_schema", js);
            cJSON_AddItemToObject(body, "response_format", rf);
        } else {
            cJSON *rf = cJSON_CreateObject();
            cJSON_AddStringToObject(rf, "type", "json_object");
            cJSON_AddItemToObject(body, "response_format", rf);
        }
    }

    return body;
}

char *structured_output_inject_prompt(const char *system_prompt, const cJSON *schema) {
    if (!system_prompt)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, system_prompt);
    strbuf_append_str(&sb, JSON_MODE_INSTRUCTION);

    if (schema) {
        char *schema_str = cJSON_Print(schema);
        if (schema_str) {
            strbuf_append_str(&sb, "\nRequired JSON schema:\n```json\n");
            strbuf_append_str(&sb, schema_str);
            strbuf_append_str(&sb, "\n```\n");
            free(schema_str);
        }
    }

    return strbuf_detach(&sb);
}

bool structured_output_validate(const char *output, const cJSON *schema) {
    if (!output)
        return false;

    cJSON *parsed = cJSON_Parse(output);
    if (!parsed)
        return false;

    if (!schema) {
        cJSON_Delete(parsed);
        return true;
    }

    /* Basic structural validation: check required top-level keys */
    cJSON *required = cJSON_GetObjectItem(schema, "required");
    if (required && cJSON_IsArray(required)) {
        int n = cJSON_GetArraySize(required);
        for (int i = 0; i < n; i++) {
            cJSON *key = cJSON_GetArrayItem(required, i);
            if (key && cJSON_IsString(key)) {
                if (!cJSON_HasObjectItem(parsed, key->valuestring)) {
                    cJSON_Delete(parsed);
                    return false;
                }
            }
        }
    }

    /* Check type constraints for top-level properties */
    cJSON *properties = cJSON_GetObjectItem(schema, "properties");
    if (properties) {
        cJSON *prop = properties->child;
        while (prop) {
            cJSON *val = cJSON_GetObjectItem(parsed, prop->string);
            if (val) {
                cJSON *type_spec = cJSON_GetObjectItem(prop, "type");
                if (type_spec && cJSON_IsString(type_spec)) {
                    const char *t = type_spec->valuestring;
                    bool ok = true;
                    if (strcmp(t, "string") == 0 && !cJSON_IsString(val))
                        ok = false;
                    if (strcmp(t, "number") == 0 && !cJSON_IsNumber(val))
                        ok = false;
                    if (strcmp(t, "integer") == 0 && !cJSON_IsNumber(val))
                        ok = false;
                    if (strcmp(t, "boolean") == 0 && !cJSON_IsBool(val))
                        ok = false;
                    if (strcmp(t, "array") == 0 && !cJSON_IsArray(val))
                        ok = false;
                    if (strcmp(t, "object") == 0 && !cJSON_IsObject(val))
                        ok = false;
                    if (!ok) {
                        cJSON_Delete(parsed);
                        return false;
                    }
                }
            }
            prop = prop->next;
        }
    }

    cJSON_Delete(parsed);
    return true;
}

char *structured_output_extract_json(const char *response) {
    if (!response)
        return NULL;

    /* Try direct parse */
    cJSON *j = cJSON_Parse(response);
    if (j)
        return nc_cjson_detach(j);

    /* Try to extract from markdown code fence */
    const char *start = strstr(response, "```json");
    if (start) {
        start += 7;
        while (*start == '\n' || *start == '\r')
            start++;
    } else {
        start = strstr(response, "```");
        if (start) {
            start += 3;
            while (*start == '\n' || *start == '\r')
                start++;
        }
    }

    if (start) {
        const char *end = strstr(start, "```");
        if (end) {
            size_t len = (size_t)(end - start);
            char *buf = malloc(len + 1);
            if (!buf)
                return NULL;
            memcpy(buf, start, len);
            buf[len] = '\0';

            /* Trim trailing whitespace */
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' '))
                buf[--len] = '\0';

            j = cJSON_Parse(buf);
            if (j) {
                char *out = nc_cjson_detach(j);
                free(buf);
                return out;
            }
            free(buf);
        }
    }

    /* Try to find first { or [ and parse from there */
    const char *p = response;
    while (*p && *p != '{' && *p != '[')
        p++;
    if (*p) {
        j = cJSON_Parse(p);
        if (j)
            return nc_cjson_detach(j);
    }

    return NULL;
}
