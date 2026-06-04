/*
 * model_presets.c — Named bundles of model / token / effort settings.
 */

#include "nevercode.h"
#include "defaults.h"

typedef struct {
    char name[PRESET_NAME_MAX];
    char model[MODEL_NAME_MAX];
    char hint[PRESET_HINT_MAX];
    int max_tokens;
    int context_window;
    effort_level_t effort;
    bool follow_provider_default_model;
    bool thinking;
    double temperature;
    double top_p;
} model_preset_entry_t;

static struct {
    model_preset_entry_t entries[PRESET_MAX_ENTRIES];
    int count;
    int active;
    provider_type_t provider;
    bool initialized;
} g_mp;

static const struct {
    const char *name;
    const char *model;
    int max_tokens;
    int context_window;
    effort_level_t effort;
    bool follow_provider_default_model;
    bool thinking;
    double temperature;
    double top_p;
    const char *hint;
} k_builtin[] = {
    {"default", NULL, DEFAULT_MAX_TOKENS, DEFAULT_CONTEXT_FALLBACK, EFFORT_DEFAULT, true, false,
     -1.0, -1.0, "General-purpose"},
    {"fast", PRESET_FAST_MODEL, EFFORT_MEDIUM_TOKENS * 2, PRESET_FAST_CTX, EFFORT_LOW, false, false,
     -1.0, -1.0, "Quick responses"},
    {"reasoning", NULL, EFFORT_HIGH_TOKENS * 2, PRESET_REASONING_CTX, EFFORT_HIGH, true, true, 0.6,
     -1.0, "Deep reasoning (lower temp for precision)"},
    {"creative", NULL, DEFAULT_MAX_TOKENS, DEFAULT_CONTEXT_FALLBACK, EFFORT_MEDIUM, true, true, 0.9,
     0.95, "Creative tasks (higher temp for diversity)"},
    {"review", PRESET_REVIEW_MODEL, DEFAULT_MAX_TOKENS, DEFAULT_CONTEXT_FALLBACK, EFFORT_MEDIUM,
     false, false, 0.3, -1.0, "Code review (low temp for consistency)"},
    {"analysis", NULL, EFFORT_HIGH_TOKENS * 2, PRESET_REASONING_CTX, EFFORT_HIGH, true, true, 0.5,
     -1.0, "Deep analysis (balanced)"},
    {NULL, NULL, 0, 0, EFFORT_DEFAULT, false, false, -1.0, -1.0, NULL},
};

static const char *preset_effective_model(const model_preset_entry_t *p) {
    if (!p)
        return NULL;
    if (p->follow_provider_default_model)
        return nc_provider_default_model(g_mp.provider);
    return p->model[0] ? p->model : NULL;
}

void model_presets_init(void) {
    memset(&g_mp, 0, sizeof(g_mp));
    g_mp.provider = DEFAULT_PROVIDER;
    for (int i = 0; k_builtin[i].name && g_mp.count < PRESET_MAX_ENTRIES; i++) {
        model_preset_entry_t *p = &g_mp.entries[g_mp.count];
        nc_strlcpy(p->name, k_builtin[i].name, sizeof(p->name));
        if (k_builtin[i].model)
            nc_strlcpy(p->model, k_builtin[i].model, sizeof(p->model));
        nc_strlcpy(p->hint, k_builtin[i].hint, sizeof(p->hint));
        p->max_tokens = k_builtin[i].max_tokens;
        p->context_window = k_builtin[i].context_window;
        p->effort = k_builtin[i].effort;
        p->follow_provider_default_model = k_builtin[i].follow_provider_default_model;
        p->thinking = k_builtin[i].thinking;
        p->temperature = k_builtin[i].temperature;
        p->top_p = k_builtin[i].top_p;
        g_mp.count++;
    }
    g_mp.initialized = true;
}

void model_presets_set_provider(provider_type_t provider) {
    g_mp.provider = provider;
}

void model_presets_free(void) {
    memset(&g_mp, 0, sizeof(g_mp));
}

int model_preset_add(const char *name, const char *model, int max_tokens, int context_window,
                     effort_level_t effort, bool thinking, const char *hint) {
    if (!g_mp.initialized || !name || !model || g_mp.count >= PRESET_MAX_ENTRIES)
        return -1;
    for (int i = 0; i < g_mp.count; i++) {
        if (strcmp(g_mp.entries[i].name, name) != 0)
            continue;
        model_preset_entry_t *p = &g_mp.entries[i];
        nc_strlcpy(p->model, model, sizeof(p->model));
        p->max_tokens = max_tokens > 0 ? max_tokens : DEFAULT_MAX_TOKENS;
        p->context_window = context_window > 0 ? context_window : DEFAULT_CONTEXT_FALLBACK;
        p->effort = effort;
        p->follow_provider_default_model = false;
        p->thinking = thinking;
        if (hint)
            nc_strlcpy(p->hint, hint, sizeof(p->hint));
        return i;
    }
    model_preset_entry_t *p = &g_mp.entries[g_mp.count];
    nc_strlcpy(p->name, name, sizeof(p->name));
    nc_strlcpy(p->model, model, sizeof(p->model));
    p->max_tokens = max_tokens > 0 ? max_tokens : DEFAULT_MAX_TOKENS;
    p->context_window = context_window > 0 ? context_window : DEFAULT_CONTEXT_FALLBACK;
    p->effort = effort;
    p->follow_provider_default_model = false;
    p->thinking = thinking;
    if (hint)
        nc_strlcpy(p->hint, hint, sizeof(p->hint));
    g_mp.count++;
    return g_mp.count - 1;
}

int model_preset_find(const char *name) {
    if (!name)
        return -1;
    for (int i = 0; i < g_mp.count; i++) {
        if (strcmp(g_mp.entries[i].name, name) == 0)
            return i;
    }
    return -1;
}

int model_preset_activate(const char *name) {
    int i = model_preset_find(name);
    if (i < 0)
        return -1;
    g_mp.active = i;
    return 0;
}

int model_preset_active_idx(void) {
    return g_mp.active;
}

const char *model_preset_model(int idx) {
    return (idx >= 0 && idx < g_mp.count) ? preset_effective_model(&g_mp.entries[idx]) : NULL;
}

int model_preset_max_tokens(int idx) {
    return (idx >= 0 && idx < g_mp.count) ? g_mp.entries[idx].max_tokens : DEFAULT_MAX_TOKENS;
}

int model_preset_context_window(int idx) {
    return (idx >= 0 && idx < g_mp.count) ? g_mp.entries[idx].context_window
                                          : DEFAULT_CONTEXT_FALLBACK;
}

effort_level_t model_preset_effort(int idx) {
    return (idx >= 0 && idx < g_mp.count) ? g_mp.entries[idx].effort : EFFORT_DEFAULT;
}

bool model_preset_thinking(int idx) {
    return (idx >= 0 && idx < g_mp.count) ? g_mp.entries[idx].thinking : false;
}

double model_preset_temperature(int idx) {
    return (idx >= 0 && idx < g_mp.count) ? g_mp.entries[idx].temperature
                                          : SAMPLING_TEMPERATURE_DEFAULT;
}

double model_preset_top_p(int idx) {
    return (idx >= 0 && idx < g_mp.count) ? g_mp.entries[idx].top_p : SAMPLING_TOP_P_DEFAULT;
}

int model_preset_count(void) {
    return g_mp.count;
}

/* Parse effort from JSON: supports string ("xhigh") or legacy int (0=default,1=low,2=med,3=high) */
static effort_level_t parse_effort_json(const cJSON *e) {
    if (!e)
        return EFFORT_DEFAULT;
    if (cJSON_IsString(e))
        return effort_level_parse(e->valuestring);
    if (cJSON_IsNumber(e)) {
        switch (e->valueint) {
        case 0:
            return EFFORT_DEFAULT;
        case 1:
            return EFFORT_LOW;
        case 2:
            return EFFORT_MEDIUM;
        case 3:
            return EFFORT_HIGH;
        default:
            return (e->valueint > 3) ? EFFORT_HIGH : EFFORT_DEFAULT;
        }
    }
    return EFFORT_DEFAULT;
}

int model_preset_load_from_config(const cJSON *cfg) {
    if (!cfg || !cJSON_IsArray(cfg))
        return 0;
    int loaded = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, cfg) {
        cJSON *jn = cJSON_GetObjectItem(item, "name");
        cJSON *jm = cJSON_GetObjectItem(item, "model");
        if (!jn || !jm || !cJSON_IsString(jn) || !cJSON_IsString(jm))
            continue;
        cJSON *t = cJSON_GetObjectItem(item, "max_tokens");
        cJSON *c = cJSON_GetObjectItem(item, "context_window");
        cJSON *e = cJSON_GetObjectItem(item, "effort");
        cJSON *th = cJSON_GetObjectItem(item, "thinking");
        cJSON *h = cJSON_GetObjectItem(item, "hint");
        int rc = model_preset_add(jn->valuestring, jm->valuestring,
                                  (t && cJSON_IsNumber(t)) ? t->valueint : 0,
                                  (c && cJSON_IsNumber(c)) ? c->valueint : 0, parse_effort_json(e),
                                  (th && cJSON_IsBool(th)) ? cJSON_IsTrue(th) : false,
                                  (h && cJSON_IsString(h)) ? h->valuestring : NULL);
        if (rc >= 0) {
            model_preset_entry_t *p = &g_mp.entries[rc];
            cJSON *jtemp = cJSON_GetObjectItem(item, "temperature");
            if (jtemp && cJSON_IsNumber(jtemp))
                p->temperature = jtemp->valuedouble;
            cJSON *jtp = cJSON_GetObjectItem(item, "top_p");
            if (jtp && cJSON_IsNumber(jtp))
                p->top_p = jtp->valuedouble;
            loaded++;
        }
    }
    return loaded;
}

char *model_preset_format_all(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *active_label = "(none)";
    if (g_mp.count > 0 && g_mp.active >= 0 && g_mp.active < g_mp.count)
        active_label = g_mp.entries[g_mp.active].name;
    strbuf_appendf(&sb, "Model Presets: %d total (active: %s)\n", g_mp.count, active_label);
    for (int i = 0; i < g_mp.count; i++) {
        model_preset_entry_t *p = &g_mp.entries[i];
        const char *model = preset_effective_model(p);
        strbuf_appendf(&sb, "  %s%s model=%s tokens=%d ctx=%dk eff=%s%s",
                       i == g_mp.active ? "* " : "  ", p->name, model ? model : "(unset)",
                       p->max_tokens, p->context_window / 1000, effort_level_name(p->effort),
                       p->thinking ? " +thinking" : "");
        if (p->temperature >= 0)
            strbuf_appendf(&sb, " temp=%.1f", p->temperature);
        if (p->top_p >= 0)
            strbuf_appendf(&sb, " top_p=%.2f", p->top_p);
        strbuf_append_str(&sb, "\n");
        if (p->hint[0])
            strbuf_appendf(&sb, "      %s\n", p->hint);
    }
    return strbuf_detach(&sb);
}
