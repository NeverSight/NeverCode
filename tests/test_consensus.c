#include "test.h"
#include "nevercode.h"
#include "config.h"
#include "tools.h"

static int t_register(void) {
    const tool_def_t *t = tools_find("Consensus");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->name, "Consensus");
    ASSERT_TRUE(t->read_only);
    ASSERT_NOT_NULL(t->input_schema);
    return 0;
}

static int t_missing_prompt(void) {
    const tool_def_t *t = tools_find("Consensus");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, -1);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "Error");
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_with_prompt(void) {
    const tool_def_t *t = tools_find("Consensus");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "prompt", "What is 2+2?");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_with_models(void) {
    const tool_def_t *t = tools_find("Consensus");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "prompt", "Solve x^2 = 4");
    cJSON *models = cJSON_AddArrayToObject(input, "models");
    cJSON_AddItemToArray(models, cJSON_CreateString("model-a"));
    cJSON_AddItemToArray(models, cJSON_CreateString("model-b"));

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, "model-a");
    ASSERT_STR_CONTAINS(output, "model-b");
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_default_model_follows_config_provider(void) {
    const tool_def_t *t = tools_find("Consensus");
    ASSERT_NOT_NULL(t);

    config_t cfg;
    config_init(&cfg);
    cfg.provider = PROVIDER_NATIVE;

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "prompt", "ping");

    char *output = NULL;
    int rc = t->execute(input, &output, &cfg);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_STR_CONTAINS(output, nc_provider_default_model(PROVIDER_NATIVE));

    free(output);
    cJSON_Delete(input);
    config_free(&cfg);
    return 0;
}

int test_consensus_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("consensus");
    RUN_TEST(t_register);
    RUN_TEST(t_missing_prompt);
    RUN_TEST(t_with_prompt);
    RUN_TEST(t_with_models);
    RUN_TEST(t_default_model_follows_config_provider);
    TEST_RETURN_FAILED();
}
