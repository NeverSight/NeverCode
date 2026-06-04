/*
 * test_context_manager.c — context zone, model limits, and error classification
 */

#include "test.h"
#include "nevercode.h"
#include "repl.h"
#include "defaults.h"

static int test_context_zone_safe(void) {
    ASSERT_INT_EQ(get_context_zone(10000, 200000), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(0, 128000), CTX_ZONE_SAFE);
    return 0;
}

static int test_context_zone_warning(void) {
    ASSERT_INT_EQ(get_context_zone(165000, 200000), CTX_ZONE_WARNING);
    return 0;
}

static int test_context_zone_compact(void) {
    ASSERT_INT_EQ(get_context_zone(182000, 200000), CTX_ZONE_COMPACT);
    return 0;
}

static int test_context_zone_blocking(void) {
    ASSERT_INT_EQ(get_context_zone(195000, 200000), CTX_ZONE_BLOCKING);
    ASSERT_INT_EQ(get_context_zone(200000, 200000), CTX_ZONE_BLOCKING);
    return 0;
}

static int test_context_zone_zero_limit(void) {
    ASSERT_INT_EQ(get_context_zone(100000, 0), CTX_ZONE_SAFE);
    ASSERT_INT_EQ(get_context_zone(100000, -1), CTX_ZONE_SAFE);
    return 0;
}

static int test_model_limit_gpt41(void) {
    ASSERT_INT_EQ(context_limit_for_model("gpt-4.1"), CTX_WINDOW_1MI);
    ASSERT_INT_EQ(context_limit_for_model("gpt-4.1-nano"), CTX_WINDOW_1MI);
    return 0;
}

static int test_model_limit_gemini(void) {
    ASSERT_INT_EQ(context_limit_for_model("gemini-2.5-pro"), CTX_WINDOW_1MI);
    ASSERT_INT_EQ(context_limit_for_model("gemini-1.5-flash"), CTX_WINDOW_1MI);
    return 0;
}

static int test_model_limit_o_series(void) {
    ASSERT_INT_EQ(context_limit_for_model("o3"), CTX_WINDOW_200K);
    ASSERT_INT_EQ(context_limit_for_model("o4-mini"), CTX_WINDOW_200K);
    ASSERT_INT_EQ(context_limit_for_model("o1"), CTX_WINDOW_200K);
    return 0;
}

static int test_model_limit_default(void) {
    ASSERT(context_limit_for_model("unknown-model-xyz") > 0);
    ASSERT(context_limit_for_model(NULL) > 0);
    return 0;
}

static int test_model_limit_compat_default(void) {
    int lim = context_limit_for_model(DEFAULT_COMPAT_MODEL);
    ASSERT(lim >= CTX_WINDOW_128K);
    return 0;
}

static int test_model_limit_native_default(void) {
    int lim = context_limit_for_model(DEFAULT_NATIVE_MODEL);
    ASSERT(lim >= CTX_WINDOW_128K);
    return 0;
}

static int test_error_classify_context(void) {
    ASSERT_INT_EQ(classify_api_error("context_length_exceeded"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("prompt is too long"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("max_tokens_exceeded"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("too many tokens"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("reduce the length"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("context window full"), API_ERR_CONTEXT_OVERFLOW);
    return 0;
}

static int test_error_classify_rate(void) {
    ASSERT_INT_EQ(classify_api_error("429 Too Many Requests"), API_ERR_RATE_LIMIT);
    ASSERT_INT_EQ(classify_api_error("rate_limit_exceeded"), API_ERR_RATE_LIMIT);
    ASSERT_INT_EQ(classify_api_error("quota exhausted"), API_ERR_RATE_LIMIT);
    ASSERT_INT_EQ(classify_api_error("throttled by server"), API_ERR_RATE_LIMIT);
    return 0;
}

static int test_error_classify_overloaded(void) {
    ASSERT_INT_EQ(classify_api_error("overloaded_error"), API_ERR_OVERLOADED);
    ASSERT_INT_EQ(classify_api_error("503 Service Unavailable"), API_ERR_OVERLOADED);
    ASSERT_INT_EQ(classify_api_error("529 capacity exceeded"), API_ERR_OVERLOADED);
    ASSERT_INT_EQ(classify_api_error("temporarily unavailable"), API_ERR_OVERLOADED);
    ASSERT_INT_EQ(classify_api_error("server is busy"), API_ERR_OVERLOADED);
    return 0;
}

static int test_error_classify_auth(void) {
    ASSERT_INT_EQ(classify_api_error("401 Unauthorized"), API_ERR_AUTH);
    ASSERT_INT_EQ(classify_api_error("invalid_api_key"), API_ERR_AUTH);
    ASSERT_INT_EQ(classify_api_error("403 Forbidden"), API_ERR_AUTH);
    ASSERT_INT_EQ(classify_api_error("authentication failed"), API_ERR_AUTH);
    ASSERT_INT_EQ(classify_api_error("api_key_invalid"), API_ERR_AUTH);
    return 0;
}

static int test_error_classify_network(void) {
    ASSERT_INT_EQ(classify_api_error("CURLE_COULDNT_RESOLVE_HOST"), API_ERR_NETWORK);
    ASSERT_INT_EQ(classify_api_error("Connection refused"), API_ERR_NETWORK);
    ASSERT_INT_EQ(classify_api_error("timed out"), API_ERR_NETWORK);
    ASSERT_INT_EQ(classify_api_error("SSL certificate problem"), API_ERR_NETWORK);
    return 0;
}

static int test_error_classify_model(void) {
    ASSERT_INT_EQ(classify_api_error("model_not_found"), API_ERR_MODEL_NOT_FOUND);
    ASSERT_INT_EQ(classify_api_error("does not exist"), API_ERR_MODEL_NOT_FOUND);
    ASSERT_INT_EQ(classify_api_error("not available"), API_ERR_MODEL_NOT_FOUND);
    return 0;
}

static int test_error_classify_invalid(void) {
    ASSERT_INT_EQ(classify_api_error("400 Bad Request"), API_ERR_INVALID_REQUEST);
    ASSERT_INT_EQ(classify_api_error("malformed JSON body"), API_ERR_INVALID_REQUEST);
    return 0;
}

static int test_error_classify_watchdog(void) {
    ASSERT_INT_EQ(classify_api_error("turn deadline exceeded"), API_ERR_WATCHDOG);
    ASSERT_INT_EQ(classify_api_error("session context may be degraded"), API_ERR_WATCHDOG);
    ASSERT_INT_EQ(classify_api_error("wall-clock deadline reached"), API_ERR_WATCHDOG);
    return 0;
}

static int test_error_classify_unknown(void) {
    ASSERT_INT_EQ(classify_api_error("something weird"), API_ERR_UNKNOWN);
    ASSERT_INT_EQ(classify_api_error(NULL), API_ERR_UNKNOWN);
    ASSERT_INT_EQ(classify_api_error(""), API_ERR_UNKNOWN);
    return 0;
}

static int test_error_hint_notnull(void) {
    ASSERT_NOT_NULL(api_error_hint(API_ERR_CONTEXT_OVERFLOW));
    ASSERT_NOT_NULL(api_error_hint(API_ERR_RATE_LIMIT));
    ASSERT_NOT_NULL(api_error_hint(API_ERR_AUTH));
    ASSERT_NOT_NULL(api_error_hint(API_ERR_NETWORK));
    ASSERT_NOT_NULL(api_error_hint(API_ERR_WATCHDOG));
    ASSERT_NOT_NULL(api_error_hint(API_ERR_MODEL_NOT_FOUND));
    ASSERT_NOT_NULL(api_error_hint(API_ERR_INVALID_REQUEST));
    ASSERT_NULL(api_error_hint(API_ERR_UNKNOWN));
    return 0;
}

static int test_compact_instructions_global(void) {
    ASSERT_NULL(g_compact_instructions);
    g_compact_instructions = nc_xstrdup("Focus on security audit");
    ASSERT_STR_EQ(g_compact_instructions, "Focus on security audit");
    free(g_compact_instructions);
    g_compact_instructions = NULL;
    return 0;
}

static int test_model_limit_from_global_config_model(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.provider = PROVIDER_NATIVE;
    g_cfg_ptr = &cfg;
    ASSERT_INT_EQ(context_limit_for_model(config_model(g_cfg_ptr)),
                  context_limit_for_model(nc_provider_default_model(PROVIDER_NATIVE)));
    g_cfg_ptr = NULL;
    config_free(&cfg);
    return 0;
}

int test_context_manager_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("context_manager");
    RUN_TEST(test_context_zone_safe);
    RUN_TEST(test_context_zone_warning);
    RUN_TEST(test_context_zone_compact);
    RUN_TEST(test_context_zone_blocking);
    RUN_TEST(test_context_zone_zero_limit);
    RUN_TEST(test_model_limit_gpt41);
    RUN_TEST(test_model_limit_gemini);
    RUN_TEST(test_model_limit_o_series);
    RUN_TEST(test_model_limit_default);
    RUN_TEST(test_model_limit_compat_default);
    RUN_TEST(test_model_limit_native_default);
    RUN_TEST(test_error_classify_context);
    RUN_TEST(test_error_classify_rate);
    RUN_TEST(test_error_classify_overloaded);
    RUN_TEST(test_error_classify_auth);
    RUN_TEST(test_error_classify_network);
    RUN_TEST(test_error_classify_model);
    RUN_TEST(test_error_classify_invalid);
    RUN_TEST(test_error_classify_watchdog);
    RUN_TEST(test_error_classify_unknown);
    RUN_TEST(test_error_hint_notnull);
    RUN_TEST(test_compact_instructions_global);
    RUN_TEST(test_model_limit_from_global_config_model);
    TEST_RETURN_FAILED();
}
