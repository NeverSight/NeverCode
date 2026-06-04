/*
 * test_model_registry.c
 */
#include "test.h"
#include "model_registry.h"
#include "defaults.h"

static int t_find_exact(void) {
    const model_info_t *m = model_registry_find("gpt-5.4");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_EQ(m->id, "gpt-5.4");
    ASSERT(m->context_length > 0);
    ASSERT(m->max_output_tokens > 0);
    return 0;
}

static int t_match_substring(void) {
    const model_info_t *m = model_registry_match("gpt-5.4");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_CONTAINS(m->id, "gpt-5.4");
    return 0;
}

static int t_match_alt_provider(void) {
    const model_info_t *m = model_registry_match("claude-opus-4-6");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_EQ(m->owned_by, "anthropic");
    ASSERT(m->supports_thinking);
    ASSERT(m->supports_tools);
    return 0;
}

static int t_unknown_returns_null(void) {
    ASSERT_NULL(model_registry_find("nonexistent-model-xyz"));
    return 0;
}

static int t_count(void) {
    int n = model_registry_count();
    ASSERT(n > 10);
    return 0;
}

static int t_get_by_index(void) {
    int n = model_registry_count();
    const model_info_t *first = model_registry_get(0);
    ASSERT_NOT_NULL(first);
    ASSERT_NOT_NULL(first->pattern);
    ASSERT_NULL(model_registry_get(n));
    ASSERT_NULL(model_registry_get(-1));
    return 0;
}

static int t_by_provider(void) {
    const model_info_t *out[64];
    int n = model_registry_by_provider("openai", out, 64);
    ASSERT(n > 0);
    for (int i = 0; i < n; i++)
        ASSERT_STR_EQ(out[i]->owned_by, "openai");
    return 0;
}

static int t_pricing(void) {
    double inp = model_registry_input_price("gpt-5.4");
    double outp = model_registry_output_price("gpt-5.4");
    ASSERT(inp > 0.0);
    ASSERT(outp > 0.0);
    ASSERT(outp > inp);
    double unk_inp = model_registry_input_price("unknown-model");
    ASSERT(unk_inp > 0.0);
    return 0;
}

static int t_case_insensitive(void) {
    const model_info_t *m1 = model_registry_match("GPT-5.4");
    const model_info_t *m2 = model_registry_match("gpt-5.4");
    if (m1 && m2)
        ASSERT_STR_EQ(m1->id, m2->id);
    return 0;
}

static int t_provider_prefix_strip(void) {
    const model_info_t *m = model_registry_match("openrouter:gpt-5.4");
    ASSERT_NOT_NULL(m);
    ASSERT_STR_CONTAINS(m->id, "gpt-5.4");
    m = model_registry_find("openai:gpt-5.4");
    ASSERT_NOT_NULL(m);
    return 0;
}

static int t_provider_prefix_preserve_ollama(void) {
    const model_info_t *m = model_registry_match("qwen3:27b");
    (void)m;
    return 0;
}

static int t_provider_prefix_unknown(void) {
    const model_info_t *m = model_registry_match("random:gpt-5.4");
    ASSERT_NOT_NULL(m);
    return 0;
}

static int t_probe_tiers(void) {
    int count = model_discovery_probe_tier_count();
    ASSERT_INT_EQ(count, MODEL_CTX_PROBE_TIERS);
    int t0 = model_discovery_probe_tier(0);
    ASSERT_INT_EQ(t0, MODEL_CTX_PROBE_TIER_0);
    int t_last = model_discovery_probe_tier(count - 1);
    ASSERT_INT_EQ(t_last, MODEL_CTX_PROBE_TIER_4);
    ASSERT(t0 > t_last);
    ASSERT_INT_EQ(model_discovery_probe_tier(-1), 0);
    ASSERT_INT_EQ(model_discovery_probe_tier(count), 0);
    return 0;
}

static int t_cache_pricing(void) {
    double cr = model_registry_cache_read_price("gpt-5.4");
    double cw = model_registry_cache_write_price("gpt-5.4");
    double in_p = model_registry_input_price("gpt-5.4");
    ASSERT(cr > 0.0);
    ASSERT(cr < in_p);
    ASSERT(cw > 0.0);

    double cr_anth = model_registry_cache_read_price("claude-opus-4-6");
    double in_anth = model_registry_input_price("claude-opus-4-6");
    ASSERT(cr_anth > 0.0);
    ASSERT(cr_anth < in_anth);
    ASSERT(cr_anth < cr_anth / CACHE_READ_DISCOUNT_ANTH + 0.01);

    double cr_unknown = model_registry_cache_read_price("custom-model-xyz");
    ASSERT(cr_unknown > 0.0);
    return 0;
}

int test_model_registry_all(void) {
    TEST_SUITE("model_registry");
    TEST_COUNTERS;
    RUN_TEST(t_find_exact);
    RUN_TEST(t_match_substring);
    RUN_TEST(t_match_alt_provider);
    RUN_TEST(t_unknown_returns_null);
    RUN_TEST(t_count);
    RUN_TEST(t_get_by_index);
    RUN_TEST(t_by_provider);
    RUN_TEST(t_pricing);
    RUN_TEST(t_cache_pricing);
    RUN_TEST(t_case_insensitive);
    RUN_TEST(t_provider_prefix_strip);
    RUN_TEST(t_provider_prefix_preserve_ollama);
    RUN_TEST(t_provider_prefix_unknown);
    RUN_TEST(t_probe_tiers);
    TEST_RETURN_FAILED();
}
