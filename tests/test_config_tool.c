#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include "config.h"

extern config_t *g_cfg_ptr;

static int test_config_tool_registered(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("ConfigTool");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == true);
    ASSERT(t->destructive == false);
    return 0;
}

static int test_config_tool_list(void) {
    config_t cfg;
    config_init(&cfg);
    g_cfg_ptr = &cfg;

    tools_register_all();
    const tool_def_t *t = tools_find("ConfigTool");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "action", "list");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "model") != NULL);
    ASSERT(strstr(output, "provider") != NULL);
    free(output);
    cJSON_Delete(input);
    config_free(&cfg);
    g_cfg_ptr = NULL;
    return 0;
}

static int test_config_tool_get(void) {
    config_t cfg;
    config_init(&cfg);
    g_cfg_ptr = &cfg;

    tools_register_all();
    const tool_def_t *t = tools_find("ConfigTool");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "action", "get");
    cJSON_AddStringToObject(input, "key", "model");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    config_free(&cfg);
    g_cfg_ptr = NULL;
    return 0;
}

static int test_config_tool_set_model(void) {
    config_t cfg;
    config_init(&cfg);
    g_cfg_ptr = &cfg;

    tools_register_all();
    const tool_def_t *t = tools_find("ConfigTool");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "action", "set");
    cJSON_AddStringToObject(input, "key", "model");
    cJSON_AddStringToObject(input, "value", "gpt-5.4-turbo");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "gpt-5.4-turbo") != NULL);
    ASSERT_STR_EQ(config_model(&cfg), "gpt-5.4-turbo");
    free(output);
    cJSON_Delete(input);
    config_free(&cfg);
    g_cfg_ptr = NULL;
    return 0;
}

static int test_config_tool_set_effort(void) {
    config_t cfg;
    config_init(&cfg);
    g_cfg_ptr = &cfg;

    tools_register_all();
    const tool_def_t *t = tools_find("ConfigTool");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "action", "set");
    cJSON_AddStringToObject(input, "key", "effort");
    cJSON_AddStringToObject(input, "value", "high");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.effort, EFFORT_HIGH);
    free(output);
    cJSON_Delete(input);
    config_free(&cfg);
    g_cfg_ptr = NULL;
    return 0;
}

static int test_config_tool_set_provider_updates_presets(void) {
    config_t cfg;
    config_init(&cfg);
    g_cfg_ptr = &cfg;
    model_presets_init();
    model_presets_set_provider(cfg.provider);

    tools_register_all();
    const tool_def_t *t = tools_find("ConfigTool");

    int def = model_preset_find("default");
    ASSERT_GE(def, 0);
    ASSERT_STR_EQ(model_preset_model(def), nc_provider_default_model(cfg.provider));

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "action", "set");
    cJSON_AddStringToObject(input, "key", "provider");
    cJSON_AddStringToObject(input, "value", "Anthropic");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.provider, PROVIDER_NATIVE);
    ASSERT_STR_EQ(model_preset_model(def), nc_provider_default_model(cfg.provider));
    ASSERT(strstr(output, "native") != NULL);
    free(output);
    cJSON_Delete(input);
    model_presets_free();
    config_free(&cfg);
    g_cfg_ptr = NULL;
    return 0;
}

static int test_config_tool_invalid_key(void) {
    config_t cfg;
    config_init(&cfg);
    g_cfg_ptr = &cfg;

    tools_register_all();
    const tool_def_t *t = tools_find("ConfigTool");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "action", "get");
    cJSON_AddStringToObject(input, "key", "nonexistent");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "unknown") != NULL);
    free(output);
    cJSON_Delete(input);
    config_free(&cfg);
    g_cfg_ptr = NULL;
    return 0;
}

static int test_config_tool_missing_action(void) {
    config_t cfg;
    config_init(&cfg);
    g_cfg_ptr = &cfg;

    tools_register_all();
    const tool_def_t *t = tools_find("ConfigTool");

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "missing") != NULL);
    free(output);
    cJSON_Delete(input);
    config_free(&cfg);
    g_cfg_ptr = NULL;
    return 0;
}

static int test_config_tool_set_max_turns(void) {
    config_t cfg;
    config_init(&cfg);
    g_cfg_ptr = &cfg;

    tools_register_all();
    const tool_def_t *t = tools_find("ConfigTool");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "action", "set");
    cJSON_AddStringToObject(input, "key", "max_turns");
    cJSON_AddStringToObject(input, "value", "100");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.max_turns, 100);
    free(output);
    cJSON_Delete(input);
    config_free(&cfg);
    g_cfg_ptr = NULL;
    return 0;
}

static int test_sleep_tool_registered(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("Sleep");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);
    ASSERT(t->destructive == false);
    return 0;
}

int test_config_tool_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("config_tool");

    RUN_TEST(test_config_tool_registered);
    RUN_TEST(test_config_tool_list);
    RUN_TEST(test_config_tool_get);
    RUN_TEST(test_config_tool_set_model);
    RUN_TEST(test_config_tool_set_effort);
    RUN_TEST(test_config_tool_set_provider_updates_presets);
    RUN_TEST(test_config_tool_invalid_key);
    RUN_TEST(test_config_tool_missing_action);
    RUN_TEST(test_config_tool_set_max_turns);
    RUN_TEST(test_sleep_tool_registered);

    TEST_RETURN_FAILED();
}
