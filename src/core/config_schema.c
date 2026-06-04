/*
 * config_schema.c — Configuration schema validation.
 *
 * Validates config.json files against the expected structure, providing
 * clear error messages for missing, misspelled, or invalid fields.
 */

#include "nevercode.h"

/* SCHEMA_MAX_ERRORS, SCHEMA_KEY_MAX in defaults.h */

typedef enum {
    STYPE_STRING = 0,
    STYPE_INT,
    STYPE_BOOL,
    STYPE_ARRAY,
    STYPE_OBJECT,
    STYPE_ANY
} schema_type_t;

typedef struct {
    const char *key;
    schema_type_t type;
    bool required;
} schema_field_t;

typedef struct {
    char *path;
    char *message;
} schema_error_t;

static const schema_field_t g_top_fields[] = {{"model", STYPE_STRING, false},
                                              {"provider", STYPE_STRING, false},
                                              {"base_url", STYPE_STRING, false},
                                              {"api_key", STYPE_STRING, false},
                                              {"max_tokens", STYPE_INT, false},
                                              {"max_turns", STYPE_INT, false},
                                              {"context_window", STYPE_INT, false},
                                              {"seed", STYPE_INT, false},
                                              {"stop", STYPE_ARRAY, false},
                                              {"temperature", STYPE_INT, false},
                                              {"thinking", STYPE_BOOL, false},
                                              {"effort", STYPE_STRING, false},
                                              {"tools", STYPE_ARRAY, false},
                                              {"disabled_tools", STYPE_ARRAY, false},
                                              {"hooks", STYPE_OBJECT, false},
                                              {"permissions", STYPE_OBJECT, false},
                                              {"network", STYPE_OBJECT, false},
                                              {"mcp", STYPE_OBJECT, false},
                                              {"memory", STYPE_OBJECT, false},
                                              {"presets", STYPE_OBJECT, false},
                                              {"fallback_providers", STYPE_ARRAY, false},
                                              {"vim_mode", STYPE_BOOL, false},
                                              {"output_style", STYPE_STRING, false},
                                              {"auto_compact", STYPE_BOOL, false},
                                              {"compact_template", STYPE_STRING, false},
                                              {"project_root", STYPE_STRING, false},
                                              {"sandbox_root", STYPE_STRING, false},
                                              {"exec_policy", STYPE_STRING, false},
                                              {"code_mode", STYPE_BOOL, false},
                                              {"plugins", STYPE_ARRAY, false},
                                              {"agent_roles", STYPE_OBJECT, false},
                                              {"model_router", STYPE_OBJECT, false},
                                              {"keybindings", STYPE_OBJECT, false},
                                              {"prompt_suggest", STYPE_BOOL, false},
                                              {"away_summary", STYPE_BOOL, false},
                                              {"session_memory", STYPE_BOOL, false},
                                              {"sleep_inhibit", STYPE_BOOL, false},
                                              {"native_base_url", STYPE_STRING, false},
                                              {"custom_instructions", STYPE_STRING, false},
                                              {"text_verbosity", STYPE_STRING, false},
                                              {"reasoning_summary", STYPE_STRING, false},
                                              {"default_effort", STYPE_ANY, false},
                                              {"model_aliases", STYPE_OBJECT, false},
                                              {"model_presets", STYPE_ARRAY, false},
                                              {"append_system_prompt_file", STYPE_STRING, false},
                                              {"key_pools", STYPE_ARRAY, false},
                                              {NULL, 0, false}};

static struct {
    schema_error_t errors[SCHEMA_MAX_ERRORS];
    int count;
} g_schema;

void config_schema_init(void) {
    for (int i = 0; i < g_schema.count; i++) {
        free(g_schema.errors[i].path);
        free(g_schema.errors[i].message);
    }
    g_schema.count = 0;
}

static void _add_error(const char *path, const char *msg) {
    if (g_schema.count >= SCHEMA_MAX_ERRORS)
        return;
    schema_error_t *e = &g_schema.errors[g_schema.count++];
    e->path = nc_xstrdup(path);
    e->message = nc_xstrdup(msg);
}

static bool _type_matches(const cJSON *item, schema_type_t expected) {
    switch (expected) {
    case STYPE_STRING:
        return cJSON_IsString(item);
    case STYPE_INT:
        return cJSON_IsNumber(item);
    case STYPE_BOOL:
        return cJSON_IsBool(item);
    case STYPE_ARRAY:
        return cJSON_IsArray(item);
    case STYPE_OBJECT:
        return cJSON_IsObject(item);
    case STYPE_ANY:
        return true;
    }
    return false;
}

static const char *_type_name(schema_type_t t) {
    switch (t) {
    case STYPE_STRING:
        return "string";
    case STYPE_INT:
        return "number";
    case STYPE_BOOL:
        return "boolean";
    case STYPE_ARRAY:
        return "array";
    case STYPE_OBJECT:
        return "object";
    case STYPE_ANY:
        return "any";
    }
    return "unknown";
}

static int _fuzzy_distance(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > SCHEMA_FUZZY_MAX_LEN || lb > SCHEMA_FUZZY_MAX_LEN)
        return 999;
    int dp[SCHEMA_FUZZY_MAX_LEN + 1][SCHEMA_FUZZY_MAX_LEN + 1];
    for (int i = 0; i <= la; i++)
        dp[i][0] = i;
    for (int j = 0; j <= lb; j++)
        dp[0][j] = j;
    for (int i = 1; i <= la; i++)
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int d = dp[i - 1][j] + 1;
            if (dp[i][j - 1] + 1 < d)
                d = dp[i][j - 1] + 1;
            if (dp[i - 1][j - 1] + cost < d)
                d = dp[i - 1][j - 1] + cost;
            dp[i][j] = d;
        }
    return dp[la][lb];
}

int config_schema_validate(const cJSON *config) {
    config_schema_init();
    if (!config || !cJSON_IsObject(config)) {
        _add_error("$", "root must be a JSON object");
        return g_schema.count;
    }

    for (int i = 0; g_top_fields[i].key; i++) {
        const schema_field_t *f = &g_top_fields[i];
        cJSON *item = cJSON_GetObjectItem(config, f->key);
        if (!item) {
            if (f->required) {
                char msg[NC_LABEL_MAX];
                snprintf(msg, sizeof(msg), "required field '%s' is missing", f->key);
                _add_error("$", msg);
            }
            continue;
        }
        if (!_type_matches(item, f->type)) {
            char msg[NC_LABEL_MAX];
            snprintf(msg, sizeof(msg), "field '%s' should be %s", f->key, _type_name(f->type));
            _add_error(f->key, msg);
        }
    }

    cJSON *child = config->child;
    while (child) {
        bool known = false;
        for (int i = 0; g_top_fields[i].key; i++) {
            if (strcmp(child->string, g_top_fields[i].key) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            const char *best = NULL;
            int best_dist = SCHEMA_FUZZY_MAX_DIST;
            for (int i = 0; g_top_fields[i].key; i++) {
                int d = _fuzzy_distance(child->string, g_top_fields[i].key);
                if (d < best_dist) {
                    best_dist = d;
                    best = g_top_fields[i].key;
                }
            }
            char msg[NC_LABEL_MAX];
            if (best)
                snprintf(msg, sizeof(msg), "unknown field '%s' — did you mean '%s'?", child->string,
                         best);
            else
                snprintf(msg, sizeof(msg), "unknown field '%s'", child->string);
            _add_error(child->string, msg);
        }
        child = child->next;
    }
    return g_schema.count;
}

int config_schema_error_count(void) {
    return g_schema.count;
}

const char *config_schema_error_path(int idx) {
    if (idx < 0 || idx >= g_schema.count)
        return NULL;
    return g_schema.errors[idx].path;
}

const char *config_schema_error_msg(int idx) {
    if (idx < 0 || idx >= g_schema.count)
        return NULL;
    return g_schema.errors[idx].message;
}

char *config_schema_format_errors(void) {
    if (g_schema.count == 0)
        return nc_xstrdup("Config validation: no errors\n");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Config validation: %d issue(s)\n", g_schema.count);
    for (int i = 0; i < g_schema.count; i++)
        strbuf_appendf(&sb, "  [%s] %s\n", g_schema.errors[i].path, g_schema.errors[i].message);
    return strbuf_detach(&sb);
}
