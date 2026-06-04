#include "test.h"
#include "nevercode.h"
#include "config.h"

static int t_config_defaults(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT_TRUE(cfg.temperature < 0);
    ASSERT_TRUE(cfg.top_p < 0);
    ASSERT_TRUE(cfg.frequency_penalty == 0.0);
    ASSERT_TRUE(cfg.presence_penalty == 0.0);
    config_free(&cfg);
    return 0;
}

static int t_preset_temperature(void) {
    model_presets_init();
    int ri = model_preset_find("reasoning");
    ASSERT_TRUE(ri >= 0);
    double t = model_preset_temperature(ri);
    ASSERT_TRUE(t >= 0 && t <= 1.0);

    int ci = model_preset_find("creative");
    ASSERT_TRUE(ci >= 0);
    double ct = model_preset_temperature(ci);
    ASSERT_TRUE(ct > t);

    model_presets_free();
    return 0;
}

static int t_preset_top_p(void) {
    model_presets_init();
    int ci = model_preset_find("creative");
    ASSERT_TRUE(ci >= 0);
    double tp = model_preset_top_p(ci);
    ASSERT_TRUE(tp > 0 && tp <= 1.0);

    int di = model_preset_find("default");
    ASSERT_TRUE(di >= 0);
    double dtp = model_preset_top_p(di);
    ASSERT_TRUE(dtp < 0);

    model_presets_free();
    return 0;
}

static int t_invalid_index(void) {
    model_presets_init();
    ASSERT_TRUE(model_preset_temperature(-1) < 0);
    ASSERT_TRUE(model_preset_temperature(999) < 0);
    ASSERT_TRUE(model_preset_top_p(-1) < 0);
    model_presets_free();
    return 0;
}

static int t_seed_default(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT_INT_EQ(cfg.seed, SAMPLING_SEED_DEFAULT);
    ASSERT_INT_EQ(cfg.context_window, 0);
    ASSERT_INT_EQ(cfg.num_stop_sequences, 0);
    config_free(&cfg);
    return 0;
}

static int t_stop_sequences(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT_INT_EQ(cfg.num_stop_sequences, 0);
    ASSERT_TRUE(NC_MAX_STOP_SEQUENCES >= 4);
    config_free(&cfg);
    return 0;
}

static int t_penalty_range(void) {
    ASSERT_TRUE(SAMPLING_PENALTY_MAX >= 2.0);
    ASSERT_TRUE(SAMPLING_FREQ_PENALTY_DEFAULT == 0.0);
    ASSERT_TRUE(SAMPLING_PRES_PENALTY_DEFAULT == 0.0);
    return 0;
}

static int t_preset_json_load(void) {
    model_presets_init();
    cJSON *arr = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "name", "custom");
    cJSON_AddStringToObject(item, "model", "test-model");
    cJSON_AddNumberToObject(item, "temperature", 0.7);
    cJSON_AddNumberToObject(item, "top_p", 0.85);
    cJSON_AddItemToArray(arr, item);

    int loaded = model_preset_load_from_config(arr);
    ASSERT_INT_EQ(loaded, 1);

    int idx = model_preset_find("custom");
    ASSERT_TRUE(idx >= 0);
    ASSERT_TRUE(model_preset_temperature(idx) > 0.69 && model_preset_temperature(idx) < 0.71);
    ASSERT_TRUE(model_preset_top_p(idx) > 0.84 && model_preset_top_p(idx) < 0.86);

    cJSON_Delete(arr);
    model_presets_free();
    return 0;
}

int test_sampling_params_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("sampling_params");
    RUN_TEST(t_config_defaults);
    RUN_TEST(t_preset_temperature);
    RUN_TEST(t_preset_top_p);
    RUN_TEST(t_invalid_index);
    RUN_TEST(t_seed_default);
    RUN_TEST(t_stop_sequences);
    RUN_TEST(t_penalty_range);
    RUN_TEST(t_preset_json_load);
    TEST_RETURN_FAILED();
}
