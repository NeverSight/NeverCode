/*
 * model_registry.h — Static model metadata catalog
 *
 * Centralized registry of AI model definitions, capabilities, thinking
 * configuration, and pricing.  Other modules should query the registry
 * instead of maintaining their own hardcoded model tables.
 */

#ifndef MODEL_REGISTRY_H
#define MODEL_REGISTRY_H

#include <stdbool.h>

#define MODEL_THINKING_MAX_LEVELS 8

typedef struct {
    int min;
    int max;
    bool zero_allowed;
    bool dynamic_allowed;
    bool adaptive; /* model supports adaptive thinking (auto-adjusts depth) */
    const char *levels[MODEL_THINKING_MAX_LEVELS];
    int num_levels;
} model_thinking_t;

typedef struct {
    const char *pattern; /* substring for matching (case-insensitive) */
    const char *id;      /* canonical full model ID */
    const char *display_name;
    const char *description;
    const char *owned_by; /* provider identifier */
    const char *version;
    long created; /* unix timestamp */

    int context_length;
    int max_output_tokens;
    int max_input_tokens; /* 0 = derived as context_length - max_output_tokens */

    bool supports_thinking;
    bool supports_vision;
    bool supports_tools;
    bool supports_json_mode;
    bool uses_developer_role;
    bool uses_max_completion_tokens_param;

    model_thinking_t thinking;

    double price_input; /* USD per million tokens */
    double price_output;
    double price_cache_read;  /* 0 = use CACHE_READ_DISCOUNT * price_input */
    double price_cache_write; /* 0 = same as price_input */
} model_info_t;

/* Find model by exact canonical ID */
const model_info_t *model_registry_find(const char *id);

/* Match model by substring pattern (case-insensitive, first match wins) */
const model_info_t *model_registry_match(const char *name);

/* Total number of models in the registry */
int model_registry_count(void);

/* Get model at a given index (0-based) */
const model_info_t *model_registry_get(int index);

/* Collect models whose owned_by matches a provider name */
int model_registry_by_provider(const char *owned_by, const model_info_t **out, int max);

/* Effective input limit: explicit max_input_tokens or context - output */
int model_registry_input_limit(const char *model);

/* Pricing lookup with sensible defaults for unknown models */
double model_registry_input_price(const char *model);
double model_registry_output_price(const char *model);
double model_registry_cache_read_price(const char *model);
double model_registry_cache_write_price(const char *model);

/* Context probe tiers for unknown model fallback */
int model_discovery_probe_tier(int tier_idx);
int model_discovery_probe_tier_count(void);

#endif /* MODEL_REGISTRY_H */
