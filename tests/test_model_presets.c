/*
 * tests for model_presets
 */

#include "nevercode.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>

static int test_builtin_count(void) {
    model_presets_init();
    ASSERT_GE(model_preset_count(), 6);
    model_presets_free();
    return 0;
}

static int test_find_default(void) {
    model_presets_init();
    int i = model_preset_find("default");
    ASSERT_GE(i, 0);
    ASSERT_NOT_NULL(model_preset_model(i));
    model_presets_free();
    return 0;
}

static int test_activate(void) {
    model_presets_init();
    ASSERT_INT_EQ(model_preset_activate("fast"), 0);
    ASSERT_INT_EQ(model_preset_activate("nope"), -1);
    model_presets_free();
    return 0;
}

static int test_add_custom(void) {
    model_presets_init();
    int before = model_preset_count();
    int rc = model_preset_add("ctf", "gpt-5.4", 32768, 200000, EFFORT_HIGH, true, "CTF");
    ASSERT_GE(rc, 0);
    ASSERT_INT_EQ(model_preset_count(), before + 1);
    model_presets_free();
    return 0;
}

static int test_load_from_config(void) {
    model_presets_init();
    cJSON *a = cJSON_CreateArray();
    cJSON *it = cJSON_CreateObject();
    cJSON_AddStringToObject(it, "name", "x");
    cJSON_AddStringToObject(it, "model", "m");
    cJSON_AddItemToArray(a, it);
    ASSERT_INT_EQ(model_preset_load_from_config(a), 1);
    cJSON_Delete(a);
    model_presets_free();
    return 0;
}

static int test_format(void) {
    model_presets_init();
    char *f = model_preset_format_all();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Model Presets:");
    free(f);
    model_presets_free();
    return 0;
}

static int test_reasoning_thinking(void) {
    model_presets_init();
    int i = model_preset_find("reasoning");
    ASSERT_GE(i, 0);
    ASSERT_TRUE(model_preset_thinking(i));
    model_presets_free();
    return 0;
}

static int test_provider_default_presets_follow_provider(void) {
    model_presets_init();
    int def = model_preset_find("default");
    int reasoning = model_preset_find("reasoning");
    int creative = model_preset_find("creative");
    int analysis = model_preset_find("analysis");
    int fast = model_preset_find("fast");
    ASSERT_GE(def, 0);
    ASSERT_GE(reasoning, 0);
    ASSERT_GE(creative, 0);
    ASSERT_GE(analysis, 0);
    ASSERT_GE(fast, 0);

    ASSERT_STR_EQ(model_preset_model(def), nc_provider_default_model(DEFAULT_PROVIDER));
    ASSERT_STR_EQ(model_preset_model(reasoning), nc_provider_default_model(DEFAULT_PROVIDER));
    ASSERT_STR_EQ(model_preset_model(creative), nc_provider_default_model(DEFAULT_PROVIDER));
    ASSERT_STR_EQ(model_preset_model(analysis), nc_provider_default_model(DEFAULT_PROVIDER));
    ASSERT_STR_EQ(model_preset_model(fast), PRESET_FAST_MODEL);

    model_presets_set_provider(PROVIDER_NATIVE);
    ASSERT_STR_EQ(model_preset_model(def), DEFAULT_NATIVE_MODEL);
    ASSERT_STR_EQ(model_preset_model(reasoning), DEFAULT_NATIVE_MODEL);
    ASSERT_STR_EQ(model_preset_model(creative), DEFAULT_NATIVE_MODEL);
    ASSERT_STR_EQ(model_preset_model(analysis), DEFAULT_NATIVE_MODEL);
    ASSERT_STR_EQ(model_preset_model(fast), PRESET_FAST_MODEL);

    model_presets_free();
    return 0;
}

static int test_custom_preset_stays_fixed_across_provider_switch(void) {
    model_presets_init();
    int idx = model_preset_add("ctf", "gpt-5.4", 32768, 200000, EFFORT_HIGH, true, "CTF");
    ASSERT_GE(idx, 0);
    ASSERT_STR_EQ(model_preset_model(idx), "gpt-5.4");
    model_presets_set_provider(PROVIDER_NATIVE);
    ASSERT_STR_EQ(model_preset_model(idx), "gpt-5.4");
    model_presets_free();
    return 0;
}

int test_model_presets_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("model_presets");
    RUN_TEST(test_builtin_count);
    RUN_TEST(test_find_default);
    RUN_TEST(test_activate);
    RUN_TEST(test_add_custom);
    RUN_TEST(test_load_from_config);
    RUN_TEST(test_format);
    RUN_TEST(test_reasoning_thinking);
    RUN_TEST(test_provider_default_presets_follow_provider);
    RUN_TEST(test_custom_preset_stays_fixed_across_provider_switch);
    TEST_RETURN_FAILED();
}
