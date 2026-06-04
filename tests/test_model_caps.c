#include "test.h"
#include "nevercode.h"

static int test_thinking_supported(void) {
    ASSERT(model_supports_thinking("claude-sonnet-4-20250514") == true);
    ASSERT(model_supports_thinking("claude-3-7-sonnet-latest") == true);
    ASSERT(model_supports_thinking("claude-3.7-sonnet-20250219") == true);
    return 0;
}

static int test_thinking_unsupported(void) {
    ASSERT(model_supports_thinking("claude-3-5-haiku-latest") == false);
    ASSERT(model_supports_thinking("claude-3-haiku-20240307") == false);
    return 0;
}

static int test_gpt5_caps(void) {
    ASSERT(model_supports_thinking("gpt-5.4") == true);
    ASSERT(model_supports_vision("gpt-5.4") == true);
    ASSERT(model_supports_tools("gpt-5.4") == true);
    ASSERT(model_supports_json_mode("gpt-5.4") == true);
    ASSERT(model_context_window("gpt-5.4") >= 1000000);
    ASSERT(model_max_output("gpt-5.4") >= 32768);
    return 0;
}

static int test_gpt41_no_thinking(void) {
    ASSERT(model_supports_thinking("gpt-4.1-mini") == false);
    ASSERT(model_supports_vision("gpt-4.1-mini") == true);
    ASSERT(model_supports_tools("gpt-4.1-mini") == true);
    return 0;
}

static int test_o3_thinking(void) {
    ASSERT(model_supports_thinking("o3-mini") == true);
    ASSERT(model_supports_thinking("o3-pro") == true);
    ASSERT(model_supports_thinking("o4-mini") == true);
    return 0;
}

static int test_o_series_developer_role(void) {
    ASSERT(model_uses_developer_role("o3-mini") == true);
    ASSERT(model_uses_developer_role("o3-pro") == true);
    ASSERT(model_uses_developer_role("o4-mini") == true);
    ASSERT(model_uses_developer_role("o1") == true);
    ASSERT(model_uses_developer_role("o1-mini-2024-09-12") == true);
    ASSERT(model_uses_developer_role("gpt-5.4") == false);
    ASSERT(model_uses_developer_role("gpt-4o") == false);
    ASSERT(model_uses_developer_role("claude-sonnet-4") == false);
    ASSERT(model_uses_developer_role(NULL) == false);
    return 0;
}

static int test_o_series_max_completion_tokens(void) {
    ASSERT(model_uses_max_completion_tokens("o3") == true);
    ASSERT(model_uses_max_completion_tokens("o4-mini") == true);
    ASSERT(model_uses_max_completion_tokens("o1-mini") == true);
    ASSERT(model_uses_max_completion_tokens("gpt-5.4") == false);
    ASSERT(model_uses_max_completion_tokens("gpt-4.1") == false);
    ASSERT(model_uses_max_completion_tokens("claude-sonnet-4") == false);
    return 0;
}

static int test_gemini_caps(void) {
    ASSERT(model_supports_thinking("gemini-2.5-pro-latest") == true);
    ASSERT(model_supports_vision("gemini-2.5-flash") == true);
    ASSERT(model_context_window("gemini-2.5-pro") >= 1000000);
    return 0;
}

static int test_deepseek_caps(void) {
    ASSERT(model_supports_thinking("deepseek-r1-0528") == true);
    ASSERT(model_supports_thinking("deepseek-v3") == false);
    ASSERT(model_supports_vision("deepseek-r1") == false);
    return 0;
}

static int test_qwen_caps(void) {
    ASSERT(model_supports_thinking("qwen3-235b") == true);
    ASSERT(model_supports_thinking("qwq-32b") == true);
    ASSERT(model_supports_thinking("qwen2.5-72b") == false);
    return 0;
}

static int test_unknown_model_defaults(void) {
    ASSERT(model_context_window("totally-unknown-model") == 128000);
    ASSERT(model_max_output("totally-unknown-model") == DEFAULT_MAX_TOKENS);
    ASSERT(model_supports_tools("totally-unknown-model") == true);
    return 0;
}

static int test_describe(void) {
    char *d = model_caps_describe("claude-sonnet-4-20250514");
    ASSERT_NOT_NULL(d);
    ASSERT(strstr(d, "thinking=yes") != NULL);
    ASSERT(strstr(d, "vision=yes") != NULL);
    free(d);

    char *d2 = model_caps_describe("totally-unknown");
    ASSERT_NOT_NULL(d2);
    ASSERT(strstr(d2, "unknown") != NULL);
    free(d2);
    return 0;
}

static int test_null_model(void) {
    ASSERT(model_context_window(NULL) == 128000);
    ASSERT(model_supports_thinking(NULL) == false);
    return 0;
}

static int test_input_limit(void) {
    int il = model_input_limit("gpt-5.4");
    ASSERT(il > 0);
    int ctx = model_context_window("gpt-5.4");
    int out = model_max_output("gpt-5.4");
    ASSERT(il <= ctx);
    ASSERT(il <= ctx - out || il > 0);

    int il2 = model_input_limit("totally-unknown");
    ASSERT(il2 > 0);
    ASSERT(il2 == DEFAULT_CONTEXT_FALLBACK - DEFAULT_MAX_TOKENS);
    return 0;
}

static int test_describe_shows_input(void) {
    char *d = model_caps_describe("gpt-5.4");
    ASSERT_NOT_NULL(d);
    ASSERT(strstr(d, "input=") != NULL);
    free(d);
    return 0;
}

static int test_vision_support(void) {
    ASSERT(model_supports_vision("claude-sonnet-4-latest") == true);
    ASSERT(model_supports_vision("gpt-4o-mini-2024-07-18") == true);
    ASSERT(model_supports_vision("o1-mini-2024-09-12") == false);
    ASSERT(model_supports_vision("deepseek-r1") == false);
    return 0;
}

int test_model_caps_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("model_caps");
    RUN_TEST(test_thinking_supported);
    RUN_TEST(test_thinking_unsupported);
    RUN_TEST(test_gpt5_caps);
    RUN_TEST(test_gpt41_no_thinking);
    RUN_TEST(test_o3_thinking);
    RUN_TEST(test_o_series_developer_role);
    RUN_TEST(test_o_series_max_completion_tokens);
    RUN_TEST(test_gemini_caps);
    RUN_TEST(test_deepseek_caps);
    RUN_TEST(test_qwen_caps);
    RUN_TEST(test_unknown_model_defaults);
    RUN_TEST(test_describe);
    RUN_TEST(test_null_model);
    RUN_TEST(test_input_limit);
    RUN_TEST(test_describe_shows_input);
    RUN_TEST(test_vision_support);
    TEST_RETURN_FAILED();
}
