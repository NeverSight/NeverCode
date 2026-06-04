#include "test.h"
#include "nevercode.h"
#include "api.h"

static int test_init_builtins(void) {
    pinfo_init();
    ASSERT_INT_EQ(pinfo_count(), 2);
    ASSERT_NOT_NULL(pinfo_base_url("compat"));
    ASSERT_NOT_NULL(pinfo_base_url("native"));
    ASSERT_NULL(pinfo_base_url("nonexistent"));
    return 0;
}

static int test_register(void) {
    pinfo_init();
    ASSERT_INT_EQ(pinfo_register("ollama", "Ollama", "http://localhost:11434/v1", "OLLAMA_KEY"), 0);
    ASSERT_INT_EQ(pinfo_count(), 3);
    ASSERT_STR_CONTAINS(pinfo_base_url("ollama"), "11434");
    ASSERT_INT_EQ(pinfo_register("ollama", "dup", NULL, NULL), -1); /* duplicate */
    return 0;
}

static int test_vision_reasoning(void) {
    pinfo_init();
    ASSERT_TRUE(pinfo_has_vision("compat"));
    ASSERT_TRUE(pinfo_has_reasoning("compat"));
    pinfo_set_caps("compat", 0, 1, 1, 0);
    ASSERT_FALSE(pinfo_has_vision("compat"));
    ASSERT_FALSE(pinfo_has_reasoning("compat"));
    return 0;
}

static int test_retry_config(void) {
    pinfo_init();
    ASSERT_INT_EQ(pinfo_stream_retries("compat"), 5);
    pinfo_set_retry("compat", 10, 8, 500000);
    ASSERT_INT_EQ(pinfo_stream_retries("compat"), 10);
    ASSERT_INT_EQ(pinfo_idle_timeout_ms("compat"), 500000);
    return 0;
}

static int test_format_list(void) {
    pinfo_init();
    char *s = pinfo_format_list();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Compat API");
    ASSERT_STR_CONTAINS(s, "Native API");
    free(s);
    return 0;
}

static int test_add_header(void) {
    pinfo_init();
    ASSERT_INT_EQ(pinfo_add_header("compat", "X-Custom", "test-val"), 0);
    ASSERT_INT_EQ(pinfo_add_header("nonexistent", "X", "Y"), -1);
    return 0;
}

static int test_detect_provider(void) {
    ASSERT_STR_EQ(pinfo_detect_provider_id("https://openrouter.ai/api/v1"), "openrouter");
    ASSERT_STR_EQ(pinfo_detect_provider_id("https://inference-api.nousresearch.com/v1"), "nous");
    ASSERT_STR_EQ(pinfo_detect_provider_id("https://ai-gateway.vercel.sh/v1"), "ai-gateway");
    ASSERT_STR_EQ(pinfo_detect_provider_id("https://api.openai.com"), "compat");
    ASSERT_STR_EQ(pinfo_detect_provider_id("https://api.anthropic.com"), "native");
    ASSERT_STR_EQ(pinfo_detect_provider_id("https://my-gateway.example.com/v1"), "compat");
    ASSERT_STR_EQ(pinfo_detect_provider_id("https://custom-native.example/v1/messages"), "native");
    ASSERT_STR_EQ(pinfo_detect_provider_id("https://custom.example.com/v1"), "compat");
    ASSERT_NULL(pinfo_detect_provider_id(NULL));
    return 0;
}

static int test_auto_register(void) {
    pinfo_init();
    ASSERT_INT_EQ(pinfo_count(), 2);
    ASSERT_INT_EQ(pinfo_auto_register_from_url("https://openrouter.ai/api/v1"), 0);
    ASSERT_INT_EQ(pinfo_count(), 3);
    ASSERT_INT_EQ(pinfo_auto_register_from_url("https://openrouter.ai/api/v1"), 0);
    ASSERT_INT_EQ(pinfo_count(), 3);
    return 0;
}

int test_provider_info_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("provider_info");
    RUN_TEST(test_init_builtins);
    RUN_TEST(test_register);
    RUN_TEST(test_vision_reasoning);
    RUN_TEST(test_retry_config);
    RUN_TEST(test_format_list);
    RUN_TEST(test_add_header);
    RUN_TEST(test_detect_provider);
    RUN_TEST(test_auto_register);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("provider_info");
    RUN_TEST(test_init_builtins);
    RUN_TEST(test_register);
    RUN_TEST(test_vision_reasoning);
    RUN_TEST(test_retry_config);
    RUN_TEST(test_format_list);
    RUN_TEST(test_add_header);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
