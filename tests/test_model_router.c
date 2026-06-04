#include "test.h"
#include "nevercode.h"

static int test_init_free(void) {
    model_router_init();
    ASSERT(model_router_enabled() == false);
    model_router_free();
    return 0;
}

static int test_add_tiers(void) {
    model_router_init();
    ASSERT_INT_EQ(model_router_add_tier("gpt-4o-mini", 0), 0);
    ASSERT(model_router_enabled() == false);
    ASSERT_INT_EQ(model_router_add_tier("gpt-5.4", 2), 0);
    ASSERT(model_router_enabled() == true);
    model_router_free();
    return 0;
}

static int test_classify_simple(void) {
    task_complexity_t c = model_router_classify("what is a pointer?");
    ASSERT(c == TASK_SIMPLE);
    return 0;
}

static int test_classify_moderate(void) {
    task_complexity_t c = model_router_classify(
        "Create a simple function to calculate fibonacci numbers and write tests for it.");
    ASSERT(c == TASK_MODERATE || c == TASK_COMPLEX);
    return 0;
}

static int test_classify_complex(void) {
    task_complexity_t c = model_router_classify(
        "Refactor the entire authentication system. Migrate from session-based to JWT. "
        "Update the database schema, implement refresh tokens, add security middleware, "
        "update all API endpoints, and optimize the performance of the token verification.");
    ASSERT(c == TASK_COMPLEX || c == TASK_CREATIVE);
    return 0;
}

static int test_classify_empty(void) {
    ASSERT(model_router_classify("") == TASK_SIMPLE);
    ASSERT(model_router_classify(NULL) == TASK_SIMPLE);
    return 0;
}

static int test_select_disabled(void) {
    model_router_init();
    const char *m = model_router_select("anything");
    ASSERT_NULL(m);
    model_router_free();
    return 0;
}

static int test_select_two_tier(void) {
    model_router_init();
    model_router_add_tier("fast-model", 0);
    model_router_add_tier("big-model", 2);

    const char *m1 = model_router_select("what is rust?");
    ASSERT_NOT_NULL(m1);
    ASSERT_STR_EQ(m1, "fast-model");

    const char *m2 =
        model_router_select("Refactor the database layer for performance optimization. "
                            "Migrate from SQL to NoSQL and rewrite all queries.");
    ASSERT_NOT_NULL(m2);
    ASSERT_STR_EQ(m2, "big-model");

    model_router_free();
    return 0;
}

static int test_complexity_name(void) {
    ASSERT_STR_EQ(model_router_complexity_name(TASK_SIMPLE), "simple");
    ASSERT_STR_EQ(model_router_complexity_name(TASK_MODERATE), "moderate");
    ASSERT_STR_EQ(model_router_complexity_name(TASK_COMPLEX), "complex");
    ASSERT_STR_EQ(model_router_complexity_name(TASK_CREATIVE), "creative");
    return 0;
}

static int test_describe(void) {
    model_router_init();
    model_router_add_tier("gpt-4o-mini", 0);
    model_router_add_tier("gpt-5.4", 2);

    char *desc = model_router_describe("explain what a variable is");
    ASSERT_NOT_NULL(desc);
    ASSERT(strstr(desc, "simple") != NULL || strstr(desc, "Complexity") != NULL);
    free(desc);

    model_router_free();
    return 0;
}

static int test_format_tiers_empty(void) {
    model_router_init();
    char *s = model_router_format_tiers();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "disabled") != NULL);
    free(s);
    model_router_free();
    return 0;
}

static int test_format_tiers_with_models(void) {
    model_router_init();
    model_router_add_tier("mini", 0);
    model_router_add_tier("large", 2);
    char *s = model_router_format_tiers();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "mini") != NULL);
    ASSERT(strstr(s, "large") != NULL);
    ASSERT(strstr(s, "Tier") != NULL);
    free(s);
    model_router_free();
    return 0;
}

static int test_load_from_settings(void) {
    model_router_init();
    cJSON *settings = cJSON_CreateObject();
    cJSON *routing = cJSON_AddObjectToObject(settings, "model_routing");
    cJSON *tiers = cJSON_AddArrayToObject(routing, "tiers");

    cJSON *t0 = cJSON_CreateObject();
    cJSON_AddStringToObject(t0, "model", "fast-model");
    cJSON_AddNumberToObject(t0, "min_complexity", 0);
    cJSON_AddItemToArray(tiers, t0);

    cJSON *t1 = cJSON_CreateObject();
    cJSON_AddStringToObject(t1, "model", "power-model");
    cJSON_AddNumberToObject(t1, "min_complexity", 2);
    cJSON_AddItemToArray(tiers, t1);

    int n = model_router_load_from_settings(settings);
    ASSERT_INT_EQ(n, 2);
    ASSERT(model_router_enabled() == true);

    cJSON_Delete(settings);
    model_router_free();
    return 0;
}

static int test_load_from_null(void) {
    model_router_init();
    int n = model_router_load_from_settings(NULL);
    ASSERT_INT_EQ(n, 0);
    ASSERT(model_router_enabled() == false);
    model_router_free();
    return 0;
}

static int test_classify_security(void) {
    task_complexity_t c = model_router_classify(
        "Find the buffer overflow vulnerability in this binary and write an exploit.");
    ASSERT(c >= TASK_COMPLEX);
    return 0;
}

static int test_classify_ctf(void) {
    task_complexity_t c =
        model_router_classify("Solve this CTF challenge. Reverse engineer the binary, "
                              "find the injection vector and pwn the target.");
    ASSERT(c >= TASK_COMPLEX);
    return 0;
}

static int test_classify_short_simple(void) {
    task_complexity_t c = model_router_classify("hi");
    ASSERT(c == TASK_SIMPLE);
    return 0;
}

static int test_classify_container(void) {
    task_complexity_t c = model_router_classify(
        "Set up a docker container for the application with kubernetes deployment.");
    ASSERT(c >= TASK_MODERATE);
    return 0;
}

int test_model_router_all(void) {
    TEST_SUITE("model_router");
    TEST_COUNTERS;

    RUN_TEST(test_init_free);
    RUN_TEST(test_add_tiers);
    RUN_TEST(test_classify_simple);
    RUN_TEST(test_classify_moderate);
    RUN_TEST(test_classify_complex);
    RUN_TEST(test_classify_empty);
    RUN_TEST(test_select_disabled);
    RUN_TEST(test_select_two_tier);
    RUN_TEST(test_complexity_name);
    RUN_TEST(test_describe);
    RUN_TEST(test_format_tiers_empty);
    RUN_TEST(test_format_tiers_with_models);
    RUN_TEST(test_load_from_settings);
    RUN_TEST(test_load_from_null);
    RUN_TEST(test_classify_security);
    RUN_TEST(test_classify_ctf);
    RUN_TEST(test_classify_short_simple);
    RUN_TEST(test_classify_container);

    TEST_RETURN_FAILED();
}
