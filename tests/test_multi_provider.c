#include "test.h"
#include "nevercode.h"

static int test_init_free(void) {
    multi_provider_init();
    ASSERT_INT_EQ(multi_provider_count(), 0);
    ASSERT(!multi_provider_has_fallback());
    multi_provider_free();
    return 0;
}

static int test_add_single(void) {
    multi_provider_init();
    int rc = multi_provider_add("https://api.example.com/v1", "sk-test", "gpt-4", PROVIDER_COMPAT);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(multi_provider_count(), 1);
    ASSERT(multi_provider_has_fallback());
    ASSERT_STR_EQ(multi_provider_url(0), "https://api.example.com/v1");
    ASSERT_STR_EQ(multi_provider_key(0), "sk-test");
    ASSERT_STR_EQ(multi_provider_model(0), "gpt-4");
    ASSERT(multi_provider_active(0));
    multi_provider_free();
    return 0;
}

static int test_add_multiple(void) {
    multi_provider_init();
    multi_provider_add("https://a.com/v1", "key-a", "m-a", PROVIDER_COMPAT);
    multi_provider_add("https://b.com/v1", "key-b", NULL, PROVIDER_NATIVE);
    multi_provider_add("https://c.com/v1", "key-c", "m-c", PROVIDER_COMPAT);
    ASSERT_INT_EQ(multi_provider_count(), 3);
    ASSERT_STR_EQ(multi_provider_url(1), "https://b.com/v1");
    ASSERT_NULL(multi_provider_model(1));
    ASSERT_INT_EQ((int)multi_provider_type(1), (int)PROVIDER_NATIVE);
    multi_provider_free();
    return 0;
}

static int test_add_null_rejects(void) {
    multi_provider_init();
    ASSERT_INT_EQ(multi_provider_add(NULL, "key", NULL, PROVIDER_COMPAT), -1);
    ASSERT_INT_EQ(multi_provider_add("url", NULL, NULL, PROVIDER_COMPAT), -1);
    ASSERT_INT_EQ(multi_provider_count(), 0);
    multi_provider_free();
    return 0;
}

static int test_add_overflow(void) {
    multi_provider_init();
    for (int i = 0; i < 4; i++) {
        multi_provider_add("url", "key", NULL, PROVIDER_COMPAT);
    }
    ASSERT_INT_EQ(multi_provider_count(), 4);
    ASSERT_INT_EQ(multi_provider_add("url5", "key5", NULL, PROVIDER_COMPAT), -1);
    ASSERT_INT_EQ(multi_provider_count(), 4);
    multi_provider_free();
    return 0;
}

static int test_disable_enable(void) {
    multi_provider_init();
    multi_provider_add("url", "key", NULL, PROVIDER_COMPAT);
    ASSERT(multi_provider_active(0));

    multi_provider_disable(0);
    ASSERT(!multi_provider_active(0));

    multi_provider_enable(0);
    ASSERT(multi_provider_active(0));
    multi_provider_free();
    return 0;
}

static int test_enable_all(void) {
    multi_provider_init();
    multi_provider_add("a", "ka", NULL, PROVIDER_COMPAT);
    multi_provider_add("b", "kb", NULL, PROVIDER_COMPAT);
    multi_provider_disable(0);
    multi_provider_disable(1);
    ASSERT(!multi_provider_active(0));
    ASSERT(!multi_provider_active(1));

    multi_provider_enable_all();
    ASSERT(multi_provider_active(0));
    ASSERT(multi_provider_active(1));
    multi_provider_free();
    return 0;
}

static int test_out_of_range(void) {
    multi_provider_init();
    ASSERT_NULL(multi_provider_url(-1));
    ASSERT_NULL(multi_provider_url(0));
    ASSERT_NULL(multi_provider_key(99));
    ASSERT(!multi_provider_active(-1));
    multi_provider_free();
    return 0;
}

static int test_format_empty(void) {
    multi_provider_init();
    char *s = multi_provider_format();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "No fallback") != NULL);
    free(s);
    multi_provider_free();
    return 0;
}

static int test_format_with_providers(void) {
    multi_provider_init();
    multi_provider_add("https://api.test.com/v1", "sk-xxx", "gpt-5", PROVIDER_COMPAT);
    multi_provider_add("https://anth.test.com", "sk-anth", NULL, PROVIDER_NATIVE);
    char *s = multi_provider_format();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "api.test.com") != NULL);
    ASSERT(strstr(s, "compat") != NULL);
    ASSERT(strstr(s, "native") != NULL);
    free(s);
    multi_provider_free();
    return 0;
}

static int test_load_from_config_single(void) {
    multi_provider_init();
    cJSON *root = cJSON_CreateObject();
    cJSON *fb = cJSON_AddObjectToObject(root, "fallback");
    cJSON_AddStringToObject(fb, "base_url", "https://fb.com/v1");
    cJSON_AddStringToObject(fb, "api_key", "fb-key");
    cJSON_AddStringToObject(fb, "model", "fb-model");

    int rc = multi_provider_load_from_config(root);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_INT_EQ(multi_provider_count(), 1);
    ASSERT_STR_EQ(multi_provider_url(0), "https://fb.com/v1");
    ASSERT_STR_EQ(multi_provider_model(0), "fb-model");

    cJSON_Delete(root);
    multi_provider_free();
    return 0;
}

static int test_load_from_config_array(void) {
    multi_provider_init();
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "fallback_providers");
    cJSON *p1 = cJSON_CreateObject();
    cJSON_AddStringToObject(p1, "base_url", "https://one.com/v1");
    cJSON_AddStringToObject(p1, "api_key", "k1");
    cJSON_AddStringToObject(p1, "type", "Anthropic");
    cJSON_AddItemToArray(arr, p1);
    cJSON *p2 = cJSON_CreateObject();
    cJSON_AddStringToObject(p2, "base_url", "https://two.com/v1");
    cJSON_AddStringToObject(p2, "api_key", "k2");
    cJSON_AddStringToObject(p2, "type", "OpenAI");
    cJSON_AddItemToArray(arr, p2);

    int rc = multi_provider_load_from_config(root);
    ASSERT_INT_EQ(rc, 2);
    ASSERT_INT_EQ(multi_provider_count(), 2);
    ASSERT_INT_EQ((int)multi_provider_type(0), (int)PROVIDER_NATIVE);
    ASSERT_INT_EQ((int)multi_provider_type(1), (int)PROVIDER_COMPAT);

    cJSON_Delete(root);
    multi_provider_free();
    return 0;
}

static int test_load_from_config_null(void) {
    multi_provider_init();
    ASSERT_INT_EQ(multi_provider_load_from_config(NULL), 0);
    multi_provider_free();
    return 0;
}

int test_multi_provider_all(void) {
    TEST_SUITE("multi_provider");
    TEST_COUNTERS;

    RUN_TEST(test_init_free);
    RUN_TEST(test_add_single);
    RUN_TEST(test_add_multiple);
    RUN_TEST(test_add_null_rejects);
    RUN_TEST(test_add_overflow);
    RUN_TEST(test_disable_enable);
    RUN_TEST(test_enable_all);
    RUN_TEST(test_out_of_range);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_format_with_providers);
    RUN_TEST(test_load_from_config_single);
    RUN_TEST(test_load_from_config_array);
    RUN_TEST(test_load_from_config_null);

    TEST_RETURN_FAILED();
}
