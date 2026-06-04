/*
 * effort_level.c — Thinking effort / budget control
 *
 * Controls how much reasoning effort the model should spend.
 * Maps to different API parameters depending on provider and model:
 *
 *   Chat Completions (compat):
 *     - Reasoning models: reasoning_effort + reasoning_summary + text_verbosity
 *     - Generic: reasoning_effort (low/medium/high)
 *
 *   Messages API (native):
 *     - Models with adaptive thinking: thinking.type="adaptive"
 *     - Legacy models: thinking.type="enabled" + budget_tokens
 */

#include "nevercode.h"
#include "defaults.h"
#include "model_registry.h"

static effort_level_t s_effort = EFFORT_DEFAULT;

void effort_level_init(void) {
    s_effort = EFFORT_DEFAULT;
}

void effort_level_set(effort_level_t level) {
    s_effort = level;
}

effort_level_t effort_level_get(void) {
    return s_effort;
}

const char *effort_level_name(effort_level_t level) {
    switch (level) {
    case EFFORT_NONE:
        return "none";
    case EFFORT_MINIMAL:
        return "minimal";
    case EFFORT_LOW:
        return "low";
    case EFFORT_MEDIUM:
        return "medium";
    case EFFORT_HIGH:
        return "high";
    case EFFORT_XHIGH:
        return "xhigh";
    case EFFORT_MAX:
        return "max";
    case EFFORT_DEFAULT:
        return "default";
    }
    return "default";
}

const char *effort_level_glyph(effort_level_t level) {
    switch (level) {
    case EFFORT_NONE:
        return EFFORT_GLYPH_NONE;
    case EFFORT_MINIMAL:
        return EFFORT_GLYPH_MINIMAL;
    case EFFORT_LOW:
        return EFFORT_GLYPH_LOW;
    case EFFORT_MEDIUM:
        return EFFORT_GLYPH_MEDIUM;
    case EFFORT_HIGH:
        return EFFORT_GLYPH_HIGH;
    case EFFORT_XHIGH:
        return EFFORT_GLYPH_XHIGH;
    case EFFORT_MAX:
        return EFFORT_GLYPH_MAX;
    case EFFORT_DEFAULT:
        return EFFORT_GLYPH_MEDIUM;
    }
    return EFFORT_GLYPH_MEDIUM;
}

effort_level_t effort_level_parse(const char *s) {
    if (!s)
        return EFFORT_DEFAULT;
    if (strcasecmp(s, "none") == 0 || strcasecmp(s, "n") == 0)
        return EFFORT_NONE;
    if (strcasecmp(s, "minimal") == 0 || strcasecmp(s, "min") == 0)
        return EFFORT_MINIMAL;
    if (strcasecmp(s, "low") == 0 || strcasecmp(s, "l") == 0)
        return EFFORT_LOW;
    if (strcasecmp(s, "medium") == 0 || strcasecmp(s, "med") == 0 || strcasecmp(s, "m") == 0)
        return EFFORT_MEDIUM;
    if (strcasecmp(s, "high") == 0 || strcasecmp(s, "h") == 0)
        return EFFORT_HIGH;
    if (strcasecmp(s, "xhigh") == 0 || strcasecmp(s, "xh") == 0 || strcasecmp(s, "x") == 0)
        return EFFORT_XHIGH;
    if (strcasecmp(s, "max") == 0)
        return EFFORT_MAX;
    if (strcasecmp(s, "default") == 0 || strcasecmp(s, "d") == 0)
        return EFFORT_DEFAULT;
    return EFFORT_DEFAULT;
}

effort_level_t effort_level_cycle(effort_level_t current) {
    switch (current) {
    case EFFORT_DEFAULT:
        return EFFORT_LOW;
    case EFFORT_LOW:
        return EFFORT_MEDIUM;
    case EFFORT_MEDIUM:
        return EFFORT_HIGH;
    case EFFORT_HIGH:
        return EFFORT_XHIGH;
    case EFFORT_XHIGH:
        return EFFORT_DEFAULT;
    case EFFORT_NONE:
        return EFFORT_LOW;
    case EFFORT_MINIMAL:
        return EFFORT_LOW;
    case EFFORT_MAX:
        return EFFORT_DEFAULT;
    }
    return EFFORT_DEFAULT;
}

/* Resolve budget_tokens from registry bounds when available. */
static int budget_from_registry(const model_info_t *m, effort_level_t level) {
    int lo = m->thinking.min > 0 ? m->thinking.min : EFFORT_MINIMAL_TOKENS;
    int hi = m->thinking.max > 0 ? m->thinking.max : EFFORT_MAX_TOKENS;

    switch (level) {
    case EFFORT_NONE:
        return m->thinking.zero_allowed ? 0 : lo;
    case EFFORT_MINIMAL:
        return lo;
    case EFFORT_LOW:
        return lo + (hi - lo) / 8;
    case EFFORT_MEDIUM:
        return lo + (hi - lo) / 4;
    case EFFORT_HIGH:
        return lo + (hi - lo) / 2;
    case EFFORT_XHIGH:
        return lo + (hi - lo) * 3 / 4;
    case EFFORT_MAX:
        return hi;
    case EFFORT_DEFAULT:
        return lo + (hi - lo) / 4;
    }
    return lo + (hi - lo) / 4;
}

int effort_level_max_tokens(effort_level_t level, const char *model) {
    if (model) {
        const model_info_t *m = model_registry_match(model);
        if (m && m->supports_thinking)
            return budget_from_registry(m, level);
    }
    switch (level) {
    case EFFORT_NONE:
        return EFFORT_NONE_TOKENS;
    case EFFORT_MINIMAL:
        return EFFORT_MINIMAL_TOKENS;
    case EFFORT_LOW:
        return EFFORT_LOW_TOKENS;
    case EFFORT_MEDIUM:
        return EFFORT_MEDIUM_TOKENS;
    case EFFORT_HIGH:
        return EFFORT_HIGH_TOKENS;
    case EFFORT_XHIGH:
        return EFFORT_XHIGH_TOKENS;
    case EFFORT_MAX:
        return EFFORT_MAX_TOKENS;
    case EFFORT_DEFAULT:
        return 0;
    }
    return 0;
}

const char *effort_level_compat_value(effort_level_t level) {
    switch (level) {
    case EFFORT_NONE:
        return "none";
    case EFFORT_MINIMAL:
        return "minimal";
    case EFFORT_LOW:
        return "low";
    case EFFORT_MEDIUM:
        return "medium";
    case EFFORT_HIGH:
        return "high";
    case EFFORT_XHIGH:
        return "xhigh";
    case EFFORT_MAX:
        return "high";
    case EFFORT_DEFAULT:
        return NULL;
    }
    return NULL;
}

bool effort_level_valid_for_model(effort_level_t level, const char *model) {
    if (level == EFFORT_DEFAULT)
        return true;
    if (!model)
        return true;

    const model_info_t *m = model_registry_match(model);
    if (!m || !m->supports_thinking)
        return level >= EFFORT_LOW && level <= EFFORT_HIGH;

    if (m->thinking.num_levels > 0) {
        const char *name = effort_level_name(level);
        for (int i = 0; i < m->thinking.num_levels; i++) {
            if (m->thinking.levels[i] && strcasecmp(m->thinking.levels[i], name) == 0)
                return true;
        }
        return false;
    }
    return true;
}

/*
 * Model family detection — uses centralized patterns from defaults.h
 * and registry metadata. No version numbers hardcoded here.
 */
static bool is_gpt5_family(const char *model) {
    return model && (strstr(model, MODEL_FAMILY_GPT5) || strstr(model, MODEL_FAMILY_GPT5_ALT));
}

/* Clamp effort value to nearest supported level for this model. */
static const char *clamp_effort_for_model(const model_info_t *m, effort_level_t level,
                                          const char *val) {
    if (!m || m->thinking.num_levels <= 0)
        return val;

    for (int i = 0; i < m->thinking.num_levels; i++) {
        if (m->thinking.levels[i] && strcasecmp(m->thinking.levels[i], val) == 0)
            return val;
    }
    if (level == EFFORT_XHIGH || level == EFFORT_MAX)
        return "high";
    if (level == EFFORT_NONE || level == EFFORT_MINIMAL)
        return "low";
    return val;
}

/*
 * Apply effort parameters to a Chat Completions (compat) request body.
 * Adapts parameters based on model registry metadata:
 *   - GPT-5.x family: adds reasoning_summary
 *   - All reasoning models: sends reasoning_effort, clamped to supported levels
 */
void effort_apply_to_compat_body(cJSON *body, effort_level_t level, const char *model) {
    if (level == EFFORT_DEFAULT || !body)
        return;

    const char *val = effort_level_compat_value(level);
    if (!val)
        return;

    const model_info_t *m = model ? model_registry_match(model) : NULL;
    val = clamp_effort_for_model(m, level, val);

    cJSON_AddStringToObject(body, "reasoning_effort", val);

    if (is_gpt5_family(model)) {
        cJSON_AddStringToObject(body, "reasoning_summary", "auto");
    }
}

/*
 * Apply effort parameters to a Messages API (native) request body.
 * Uses registry metadata to decide the thinking mode:
 *   - model_thinking_t.adaptive == true → thinking.type = "adaptive"
 *   - otherwise → thinking.type = "enabled" + budget_tokens
 *
 * No model version strings are checked here; add `.adaptive = true`
 * in model_registry.c for new models that support adaptive thinking.
 */
void effort_apply_to_native_body(cJSON *body, effort_level_t level, const char *model,
                                 int max_tokens) {
    if (!body || !model || !model_supports_thinking(model))
        return;

    cJSON *thinking = cJSON_CreateObject();

    if (model_supports_adaptive_thinking(model)) {
        cJSON_AddStringToObject(thinking, "type", "adaptive");
    } else {
        cJSON_AddStringToObject(thinking, "type", "enabled");

        int budget;
        if (level == EFFORT_DEFAULT) {
            budget = max_tokens > THINKING_BUDGET_THRESHOLD ? THINKING_BUDGET_LARGE
                                                            : THINKING_BUDGET_SMALL;
        } else {
            budget = effort_level_max_tokens(level, model);
        }

        if (budget <= 0)
            budget = THINKING_BUDGET_SMALL;
        cJSON_AddNumberToObject(thinking, "budget_tokens", budget);
    }

    cJSON_AddItemToObject(body, "thinking", thinking);
}

char *effort_level_describe(void) {
    char buf[NC_LABEL_MAX];
    const char *name = effort_level_name(s_effort);

    if (s_effort == EFFORT_DEFAULT) {
        snprintf(buf, sizeof(buf), "Effort: %s (provider decides)", name);
    } else if (s_effort == EFFORT_NONE) {
        snprintf(buf, sizeof(buf), "Effort: %s (reasoning disabled)", name);
    } else {
        int tok = effort_level_max_tokens(s_effort, NULL);
        snprintf(buf, sizeof(buf),
                 "Effort: %s (compat: reasoning_effort=%s, native: ~%d thinking tokens)", name,
                 effort_level_compat_value(s_effort), tok);
    }
    return nc_xstrdup(buf);
}
