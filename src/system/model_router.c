/*
 * model_router.c — Smart model routing based on task complexity
 *
 * Analyzes user input and selects the most appropriate model:
 *   - Simple queries → fast/small model
 *   - Complex tasks → powerful model
 *   - Code generation → code-optimized model
 *
 * Users can configure model tiers via settings or CLI.
 */

#include "nevercode.h"

typedef struct {
    const char *pattern;
    int complexity_boost;
} complexity_signal_t;

static const complexity_signal_t COMPLEX_SIGNALS[] = {
    {"refactor", 2},    {"redesign", 2},      {"architect", 2},      {"implement", 1},
    {"create", 1},      {"build", 1},         {"write", 1},          {"optimize", 2},
    {"debug", 2},       {"fix", 1},           {"migrate", 2},        {"test", 1},
    {"review", 1},      {"analyze", 1},       {"performance", 2},    {"security", 2},
    {"deploy", 1},      {"integrate", 2},     {"rewrite", 2},        {"design pattern", 2},
    {"database", 1},    {"api", 1},           {"authentication", 2}, {"authorization", 2},
    {"concurrency", 2}, {"threading", 2},     {"benchmark", 1},      {"CI/CD", 1},
    {"investigate", 2}, {"exception", 1},     {"patch", 1},          {"planning", 1},
    {"docker", 1},      {"kubernetes", 1},    {"container", 1},      {"delegate", 1},
    {"compare", 1},     {"vulnerability", 2}, {"exploit", 2},        {"reverse", 1},
    {"decompile", 2},   {"disassemble", 2},   {"forensic", 2},       {"pentest", 2},
    {"ctf", 1},         {"pwn", 2},           {"overflow", 1},       {"injection", 1},
    {NULL, 0}};

static const complexity_signal_t SIMPLE_SIGNALS[] = {
    {"what is", -1}, {"explain", -1}, {"show", -1},     {"list", -1},
    {"print", -1},   {"how to", 0},   {"where is", -1}, {"help", -1},
    {"version", -2}, {"status", -2},  {NULL, 0}};

/* MAX_MODEL_TIERS in defaults.h */

typedef struct {
    char *models[MAX_MODEL_TIERS];
    int num_models;
    bool enabled;
    int complexity_threshold[MAX_MODEL_TIERS];
} model_router_t;

static model_router_t g_router = {.num_models = 0, .enabled = false};

void model_router_init(void) {
    g_router.enabled = false;
    g_router.num_models = 0;
    for (int i = 0; i < MAX_MODEL_TIERS; i++) {
        g_router.models[i] = NULL;
        g_router.complexity_threshold[i] = i;
    }
}

void model_router_free(void) {
    for (int i = 0; i < g_router.num_models; i++)
        free(g_router.models[i]);
    g_router.num_models = 0;
    g_router.enabled = false;
}

int model_router_add_tier(const char *model, int min_complexity) {
    if (g_router.num_models >= MAX_MODEL_TIERS)
        return -1;
    int idx = g_router.num_models;
    g_router.models[idx] = nc_xstrdup(model);
    g_router.complexity_threshold[idx] = min_complexity;
    g_router.num_models++;
    g_router.enabled = (g_router.num_models >= 2);
    return 0;
}

bool model_router_enabled(void) {
    return g_router.enabled;
}

task_complexity_t model_router_classify(const char *input) {
    if (!input || !*input)
        return TASK_SIMPLE;

    int score = 0;
    size_t len = strlen(input);

    /* word count heuristic: short messages are likely simple */
    int word_count = 0;
    {
        bool in_word = false;
        for (const char *p = input; *p; p++) {
            if (isspace((unsigned char)*p)) {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                word_count++;
            }
        }
    }
    if (word_count <= MROUTER_SHORT_WORD_MAX)
        score -= 1;

    /* length heuristic */
    if (len < MROUTER_SHORT_LEN)
        score -= 1;
    if (len > MROUTER_MEDIUM_LEN)
        score += 1;
    if (len > MROUTER_LONG_LEN)
        score += 1;

    /* question mark → likely inquiry */
    if (strchr(input, '?'))
        score -= 1;

    /* multiline input → more complex */
    int newlines = 0;
    for (const char *p = input; *p; p++)
        if (*p == '\n')
            newlines++;
    if (newlines > 1)
        score += 1;

    /* multiple sentences → more complex */
    int periods = 0;
    for (const char *p = input; *p; p++)
        if (*p == '.')
            periods++;
    if (periods >= MROUTER_MULTI_SENTENCE)
        score += 1;

    /* code-related markers */
    if (strstr(input, "```"))
        score += 2;
    if (strstr(input, "`") && !strstr(input, "```"))
        score += 1;
    if (strstr(input, "diff"))
        score += 1;
    if (strstr(input, "error"))
        score += 1;
    if (strstr(input, "stack trace") || strstr(input, "traceback") || strstr(input, "stacktrace"))
        score += 2;

    /* URL presence → likely complex fetch/analysis */
    if (strstr(input, "http://") || strstr(input, "https://") || strstr(input, "www."))
        score += 1;

    /* file references → real work */
    if (strchr(input, '@'))
        score += 1;

    /* complex pattern matching */
    for (int i = 0; COMPLEX_SIGNALS[i].pattern; i++) {
        if (nc_contains_ci(input, COMPLEX_SIGNALS[i].pattern))
            score += COMPLEX_SIGNALS[i].complexity_boost;
    }

    /* simple pattern matching */
    for (int i = 0; SIMPLE_SIGNALS[i].pattern; i++) {
        if (nc_contains_ci(input, SIMPLE_SIGNALS[i].pattern))
            score += SIMPLE_SIGNALS[i].complexity_boost;
    }

    /* multi-file mentions → complex */
    int at_count = 0;
    for (const char *p = input; *p; p++)
        if (*p == '@')
            at_count++;
    if (at_count >= MROUTER_MULTI_FILE_REF)
        score += 1;

    if (score <= MROUTER_SIMPLE_CEILING)
        return TASK_SIMPLE;
    if (score <= MROUTER_MODERATE_CEILING)
        return TASK_MODERATE;
    if (score <= MROUTER_COMPLEX_CEILING)
        return TASK_COMPLEX;
    return TASK_CREATIVE;
}

const char *model_router_select(const char *input) {
    if (!g_router.enabled || g_router.num_models == 0)
        return NULL;

    task_complexity_t complexity = model_router_classify(input);

    /* Find highest tier that this complexity qualifies for */
    int best = 0;
    for (int i = 0; i < g_router.num_models; i++) {
        if ((int)complexity >= g_router.complexity_threshold[i])
            best = i;
    }

    return g_router.models[best];
}

const char *model_router_complexity_name(task_complexity_t c) {
    switch (c) {
    case TASK_SIMPLE:
        return "simple";
    case TASK_MODERATE:
        return "moderate";
    case TASK_COMPLEX:
        return "complex";
    case TASK_CREATIVE:
        return "creative";
    }
    return "unknown";
}

char *model_router_describe(const char *input) {
    task_complexity_t c = model_router_classify(input);
    const char *model = model_router_select(input);

    char buf[NC_MSG_MAX];
    snprintf(buf, sizeof(buf), "Complexity: %s (level %d)%s%s", model_router_complexity_name(c),
             (int)c, model ? " → model: " : "", model ? model : "");
    return nc_xstrdup(buf);
}

char *model_router_format_tiers(void) {
    if (g_router.num_models == 0)
        return nc_xstrdup("Model routing: disabled (single model)\n");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Model Routing Tiers:\n");

    for (int i = 0; i < g_router.num_models; i++) {
        char line[NC_LABEL_MAX];
        snprintf(line, sizeof(line), "  Tier %d: %s (complexity >= %d)\n", i, g_router.models[i],
                 g_router.complexity_threshold[i]);
        strbuf_append_str(&sb, line);
    }
    return strbuf_detach(&sb);
}

int model_router_load_from_settings(const cJSON *settings) {
    if (!settings)
        return 0;

    const cJSON *routing = cJSON_GetObjectItem(settings, "model_routing");
    if (!routing)
        return 0;

    const cJSON *tiers = cJSON_GetObjectItem(routing, "tiers");
    if (!cJSON_IsArray(tiers))
        return 0;

    int n = cJSON_GetArraySize(tiers);
    for (int i = 0; i < n && i < MAX_MODEL_TIERS; i++) {
        cJSON *tier = cJSON_GetArrayItem(tiers, i);
        const char *model = cJSON_GetStringValue(cJSON_GetObjectItem(tier, "model"));
        cJSON *thresh = cJSON_GetObjectItem(tier, "min_complexity");
        int min_c = cJSON_IsNumber(thresh) ? thresh->valueint : i;

        if (model)
            model_router_add_tier(model, min_c);
    }
    return g_router.num_models;
}
