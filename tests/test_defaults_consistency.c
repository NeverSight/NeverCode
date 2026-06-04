/*
 * test_defaults_consistency.c — Verify defaults.h macros are
 * consistently used across modules and no stale local overrides exist.
 */

#include "test.h"
#include "nevercode.h"
#include "defaults.h"

extern void effort_level_init(void);
extern int effort_level_max_tokens(effort_level_t, const char *);
extern void auto_compact_init(int);
extern int auto_compact_effective_window(void);
extern void auto_compact_set_window(int, int);
extern void auto_compact_free(void);
extern int model_max_output(const char *);

static int t_effort_tokens_match_macros(void) {
    effort_level_init();
    ASSERT_INT_EQ(effort_level_max_tokens(EFFORT_LOW, NULL), EFFORT_LOW_TOKENS);
    ASSERT_INT_EQ(effort_level_max_tokens(EFFORT_MEDIUM, NULL), EFFORT_MEDIUM_TOKENS);
    ASSERT_INT_EQ(effort_level_max_tokens(EFFORT_HIGH, NULL), EFFORT_HIGH_TOKENS);
    ASSERT_INT_EQ(effort_level_max_tokens(EFFORT_XHIGH, NULL), EFFORT_XHIGH_TOKENS);
    ASSERT_INT_EQ(effort_level_max_tokens(EFFORT_MAX, NULL), EFFORT_MAX_TOKENS);
    ASSERT_INT_EQ(effort_level_max_tokens(EFFORT_DEFAULT, NULL), 0);
    return 0;
}

static int t_auto_compact_min_window(void) {
    auto_compact_init(100);
    int ew = auto_compact_effective_window();
    ASSERT_GE(ew, AUTO_COMPACT_MIN_WINDOW);
    auto_compact_free();
    return 0;
}

static int t_auto_compact_large_window(void) {
    auto_compact_init(200000);
    int ew = auto_compact_effective_window();
    ASSERT(ew == 200000 - DEFAULT_MAX_TOKENS);
    auto_compact_free();
    return 0;
}

static int t_auto_compact_set_window_floor(void) {
    auto_compact_init(200000);
    auto_compact_set_window(50, 40);
    int ew = auto_compact_effective_window();
    ASSERT_GE(ew, AUTO_COMPACT_MIN_WINDOW);
    auto_compact_free();
    return 0;
}

static int t_model_default_fallback(void) {
    ASSERT_INT_EQ(model_max_output("nonexistent-model-xyz"), DEFAULT_MAX_TOKENS);
    return 0;
}

static int t_version_str_consistent(void) {
    char expected[32];
    snprintf(expected, sizeof(expected), "%d.%d.%d", NEVERCODE_VERSION_MAJOR,
             NEVERCODE_VERSION_MINOR, NEVERCODE_VERSION_PATCH);
    ASSERT_STR_EQ(NEVERCODE_VERSION_STR, expected);
    return 0;
}

static int t_config_dirs_non_empty(void) {
    ASSERT(strlen(CONFIG_DIR_PRIMARY) > 0);
    ASSERT(strlen(CONFIG_DIR_SECONDARY) > 0);
    ASSERT(strlen(CONFIG_DIR_COMPAT) > 0);
    return 0;
}

static int t_buffer_macros_positive(void) {
    ASSERT_GT(NC_PATH_MAX, 0);
    ASSERT_GT(NC_URL_MAX, 0);
    ASSERT_GT(NC_CMD_MAX, 0);
    ASSERT_GT(NC_LINE_MAX, 0);
    ASSERT_GT(NC_ERRBUF_MAX, 0);
    ASSERT_GT(NC_MSG_MAX, 0);
    ASSERT_GT(NC_LABEL_MAX, 0);
    ASSERT_GT(NC_TAG_MAX, 0);
    return 0;
}

static int t_buffer_macros_ordered(void) {
    ASSERT_GE(NC_CMD_MAX, NC_PATH_MAX);
    ASSERT_GE(NC_PATH_MAX, NC_URL_MAX);
    ASSERT_GE(NC_URL_MAX, NC_ERRBUF_MAX);
    ASSERT_GE(NC_LINE_MAX, NC_MSG_MAX);
    ASSERT_GE(NC_MSG_MAX, NC_LABEL_MAX);
    ASSERT_GE(NC_LABEL_MAX, NC_TAG_MAX);
    ASSERT_INT_EQ(NC_MSG_MAX, 512);
    ASSERT_INT_EQ(NC_LABEL_MAX, 256);
    ASSERT_INT_EQ(NC_TAG_MAX, 128);
    return 0;
}

static int t_retry_macros_sane(void) {
    ASSERT_GT(RETRY_BASE_DELAY_MS, 0);
    ASSERT_GT(RETRY_MAX_DELAY_MS, RETRY_BASE_DELAY_MS);
    ASSERT_GT(RETRY_MAX_DEFAULT, 0);
    ASSERT_GT(RETRY_MAX_OVERLOAD, 0);
    ASSERT(RETRY_JITTER_FACTOR > 0.0 && RETRY_JITTER_FACTOR < 1.0);
    return 0;
}

static int t_live_compat_http_timeouts_sane(void) {
    ASSERT_GT(NC_LIVE_COMPAT_HTTP_TIMEOUT_S, 0);
    ASSERT_GT(NC_LIVE_COMPAT_CONNECT_TIMEOUT_S, 0);
    ASSERT(NC_LIVE_COMPAT_CONNECT_TIMEOUT_S <= NC_LIVE_COMPAT_HTTP_TIMEOUT_S);
    return 0;
}

static int t_token_escalation_macros_sane(void) {
    ASSERT_GT(NC_INITIAL_MAX_TOKENS, 0);
    ASSERT_GT(NC_ESCALATED_MAX_TOKENS, NC_INITIAL_MAX_TOKENS);
    ASSERT_GT(NC_TOKEN_ESCALATION_MAX, 0);
    ASSERT_GT(NC_PREVIEW_SPLIT_THRESHOLD, 0);
    ASSERT_GE(NC_PREVIEW_SPLIT_THRESHOLD, NC_PREVIEW_HALF * 2);
    ASSERT_GT(NC_PREVIEW_SHORT, 0);
    ASSERT_GT(NC_DAEMON_FINDINGS_CAP, 0);
    ASSERT_GT(NC_DAEMON_FILES_CAP, 0);
    ASSERT_GT(NC_MICRO_COMPACT_FLOOR, 0);
    return 0;
}

static int t_context_suggestion_macros(void) {
    ASSERT_GT(CTX_LARGE_TOOL_RESULT_TOK, 0);
    ASSERT_GT(CTX_MAX_SUGGESTIONS, 0);
    ASSERT(CTX_NEAR_CAPACITY_PCT > 0 && CTX_NEAR_CAPACITY_PCT <= 100);
    return 0;
}

static int t_away_macros_sane(void) {
    ASSERT_GT(AWAY_IDLE_SECONDS, 0);
    ASSERT_GT(AWAY_MSG_WINDOW, 0);
    ASSERT_GT(AWAY_MAX_RESPONSE_LEN, 0);
    ASSERT_GT(AWAY_MAX_PROMPT_LEN, AWAY_MAX_RESPONSE_LEN);
    return 0;
}

static int t_harness_macros_sane(void) {
    ASSERT_GT(FATIGUE_SAMPLE_MAX, 0);
    ASSERT(FATIGUE_EMA_ALPHA > 0.0 && FATIGUE_EMA_ALPHA < 1.0);
    ASSERT_GT(MAILBOX_QUEUE_CAP, 0);
    ASSERT_GT(MAX_FALLBACK_PROVIDERS, 0);
    ASSERT_GT(MAX_MODEL_TIERS, 0);
    return 0;
}

static int t_subagent_delegation_macros(void) {
    ASSERT_GT(NC_MAX_DELEGATION_DEPTH, 0);
    ASSERT(NC_MAX_DELEGATION_DEPTH <= 16);
    ASSERT_GT(NC_AGENT_DEFAULT_TURNS, 0);
    ASSERT(NC_AGENT_DEFAULT_TURNS <= DEFAULT_MAX_TURNS);
    return 0;
}

static int t_env_var_macros_non_empty(void) {
    ASSERT(strlen(ENV_HTTP_PROXY) > 0);
    ASSERT(strlen(ENV_HTTP_PROXY_LC) > 0);
    ASSERT(strlen(ENV_HTTPS_PROXY) > 0);
    ASSERT(strlen(ENV_HTTPS_PROXY_LC) > 0);
    ASSERT(strlen(ENV_ALL_PROXY) > 0);
    ASSERT(strlen(ENV_ALL_PROXY_LC) > 0);
    ASSERT(strlen(ENV_NO_PROXY) > 0);
    ASSERT(strlen(ENV_NO_PROXY_LC) > 0);
    ASSERT(strlen(ENV_COLORTERM) > 0);
    ASSERT(strlen(ENV_TMUX) > 0);
    ASSERT(strlen(ENV_STY) > 0);
    ASSERT(strlen(ENV_SSH_CONN) > 0);
    ASSERT(strlen(ENV_LANG) > 0);
    ASSERT(strlen(ENV_TZ) > 0);
    ASSERT(strlen(ENV_EFFORT) > 0);
    ASSERT(strlen(ENV_SANDBOX) > 0);
    ASSERT(strlen(ENV_COORDINATOR_MODE) > 0);
    ASSERT(strlen(ENV_REDACT_SECRETS) > 0);
    ASSERT(strlen(ENV_COMPAT_KEY) > 0);
    ASSERT(strlen(ENV_NATIVE_KEY) > 0);
    ASSERT(strlen(ENV_LD_PRELOAD) > 0);
    ASSERT(strlen(ENV_LD_LIBRARY_PATH) > 0);
    ASSERT(strlen(ENV_LD_AUDIT) > 0);
    ASSERT(strlen(ENV_DYLD_INSERT_LIBRARIES) > 0);
    ASSERT(strlen(ENV_DYLD_LIBRARY_PATH) > 0);
    ASSERT(strlen(ENV_DYLD_FRAMEWORK_PATH) > 0);
    ASSERT(strlen(ENV_HOME) > 0);
    ASSERT(strlen(ENV_USER) > 0);
    ASSERT(strlen(ENV_SHELL) > 0);
    ASSERT(strlen(ENV_TMPDIR) > 0);
    ASSERT(strlen(ENV_TERM) > 0);
    ASSERT(strlen(ENV_PATH) > 0);
    ASSERT(strlen(ENV_OPENROUTER_KEY) > 0);
    ASSERT(strlen(ENV_NOUS_KEY) > 0);
    return 0;
}

static int t_net_err_macros_non_empty(void) {
    ASSERT(strlen(NET_ERR_CURLE_PREFIX) > 0);
    ASSERT(strlen(NET_ERR_CONNECT) > 0);
    ASSERT(strlen(NET_ERR_RESOLVE) > 0);
    ASSERT(strlen(NET_ERR_DNS_FAIL) > 0);
    ASSERT(strlen(NET_ERR_CONN_REFUSED) > 0);
    ASSERT(strlen(NET_ERR_TIMED_OUT) > 0);
    ASSERT(strlen(NET_ERR_TIMEOUT) > 0);
    ASSERT(strlen(NET_ERR_NETWORK) > 0);
    ASSERT(strlen(NET_ERR_SSL) > 0);
    ASSERT(strlen(NET_ERR_CONNRESET) > 0);
    ASSERT(strlen(NET_ERR_CONNREFUSED) > 0);
    ASSERT(strlen(NET_ERR_ETIMEDOUT) > 0);
    ASSERT(strlen(NET_ERR_CONNECTION) > 0);
    ASSERT(strlen(HTTP_ERR_INTERNAL) > 0);
    ASSERT(strlen(HTTP_ERR_BAD_GW) > 0);
    ASSERT(strlen(HTTP_ERR_TOO_MANY) > 0);
    ASSERT(strlen(HTTP_ERR_TEMPORARILY) > 0);
    ASSERT(strlen(AERR_BODY_INVALID_KEY) > 0);
    ASSERT(strlen(AERR_BODY_KEY_INVALID) > 0);
    ASSERT(strlen(AERR_BODY_MODEL_MISSING) > 0);
    ASSERT(strlen(AERR_BODY_NOT_EXIST) > 0);
    ASSERT(strlen(AERR_BODY_UNKNOWN_MODEL) > 0);
    ASSERT(strlen(AERR_BODY_NOT_AVAILABLE) > 0);
    ASSERT(strlen(AERR_BODY_MALFORMED) > 0);
    ASSERT(strlen(AERR_BODY_PROMPT_LONG) > 0);
    ASSERT(strlen(AERR_BODY_TOO_LONG) > 0);
    ASSERT(strlen(AERR_BODY_TURN_DEADLINE) > 0);
    ASSERT(strlen(AERR_BODY_WC_DEADLINE) > 0);
    ASSERT(strlen(AERR_BODY_CONSEC_TURNS) > 0);
    ASSERT(strlen(AERR_BODY_CTX_DEGRADED) > 0);
    return 0;
}

static int t_cost_macros_positive(void) {
    ASSERT(COST_ANTH_OPUS_INPUT > 0);
    ASSERT(COST_ANTH_OPUS_OUTPUT > 0);
    ASSERT(COST_ANTH_SONNET_INPUT > 0);
    ASSERT(COST_ANTH_SONNET_OUTPUT > 0);
    ASSERT(COST_ANTH_HAIKU45_INPUT > 0);
    ASSERT(COST_ANTH_HAIKU45_OUTPUT > 0);
    ASSERT(COST_ANTH_HAIKU_INPUT > 0);
    ASSERT(COST_ANTH_HAIKU_OUTPUT > 0);
    ASSERT(COST_GPT5_INPUT > 0);
    ASSERT(COST_GPT5_OUTPUT > 0);
    ASSERT(COST_GPT5_MINI_INPUT > 0);
    ASSERT(COST_GPT5_MINI_OUTPUT > 0);
    ASSERT(COST_GPT4O_INPUT > 0);
    ASSERT(COST_GPT4O_OUTPUT > 0);
    ASSERT(COST_GPT4O_MINI_INPUT > 0);
    ASSERT(COST_GPT4O_MINI_OUTPUT > 0);
    ASSERT(COST_O1_INPUT > 0);
    ASSERT(COST_O1_OUTPUT > 0);
    ASSERT(COST_GEMINI_FLASH_INPUT > 0);
    ASSERT(COST_GEMINI_FLASH_OUTPUT > 0);
    ASSERT(COST_GEMINI_PRO_INPUT > 0);
    ASSERT(COST_GEMINI_PRO_OUTPUT > 0);
    ASSERT(COST_DEEPSEEK_INPUT > 0);
    ASSERT(COST_DEEPSEEK_OUTPUT > 0);
    ASSERT(COST_DEFAULT_INPUT > 0);
    ASSERT(COST_DEFAULT_OUTPUT > 0);
    ASSERT(COST_ANTH_OPUS_OUTPUT > COST_ANTH_OPUS_INPUT);
    ASSERT(COST_GPT5_OUTPUT > COST_GPT5_INPUT);
    ASSERT(COST_DEFAULT_OUTPUT > COST_DEFAULT_INPUT);
    return 0;
}

static int t_array_len_macro(void) {
    int arr[] = {10, 20, 30, 40, 50};
    ASSERT_INT_EQ(NC_ARRAY_LEN(arr), 5);
    const char *strs[] = {"a", "b", "c"};
    ASSERT_INT_EQ(NC_ARRAY_LEN(strs), 3);
    return 0;
}

static int t_output_token_macros_ordered(void) {
    ASSERT_INT_EQ(OUTPUT_TOKENS_2K, 2048);
    ASSERT_INT_EQ(OUTPUT_TOKENS_4K, 4096);
    ASSERT_INT_EQ(OUTPUT_TOKENS_8K, 8192);
    ASSERT_INT_EQ(OUTPUT_TOKENS_16K, 16384);
    ASSERT_INT_EQ(OUTPUT_TOKENS_32K, 32768);
    ASSERT_INT_EQ(OUTPUT_TOKENS_32KR, 32000);
    ASSERT_INT_EQ(OUTPUT_TOKENS_64K, 65536);
    ASSERT_INT_EQ(OUTPUT_TOKENS_64KR, 64000);
    ASSERT_INT_EQ(OUTPUT_TOKENS_100K, 100000);
    ASSERT_INT_EQ(OUTPUT_TOKENS_128K, 128000);
    ASSERT(OUTPUT_TOKENS_2K < OUTPUT_TOKENS_4K);
    ASSERT(OUTPUT_TOKENS_4K < OUTPUT_TOKENS_8K);
    ASSERT(OUTPUT_TOKENS_8K < OUTPUT_TOKENS_16K);
    ASSERT(OUTPUT_TOKENS_16K < OUTPUT_TOKENS_32K);
    ASSERT(OUTPUT_TOKENS_32K < OUTPUT_TOKENS_64K);
    ASSERT(OUTPUT_TOKENS_64K < OUTPUT_TOKENS_128K);
    return 0;
}

static int t_hash_seed_macros(void) {
    ASSERT(NC_DJB2_SEED == 5381u);
    ASSERT(NC_FNV1A_OFFSET == 2166136261u);
    ASSERT(NC_FNV1A_PRIME == 16777619u);
    ASSERT(NC_FNV1A_PRIME > 1u);
    return 0;
}

static int t_exit_code_macros(void) {
    ASSERT(NC_EXIT_TIMEOUT == 124);
    ASSERT(NC_EXIT_SIGNAL(9) == 137);
    ASSERT(NC_EXIT_SIGNAL(15) == 143);
    ASSERT(NC_EXIT_SIGNAL(SIGKILL) == NC_EXIT_SIGNAL(9));
    ASSERT(NC_EXIT_SIGNAL(0) == 128);
    return 0;
}

int test_defaults_consistency_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("defaults_consistency");
    RUN_TEST(t_effort_tokens_match_macros);
    RUN_TEST(t_auto_compact_min_window);
    RUN_TEST(t_auto_compact_large_window);
    RUN_TEST(t_auto_compact_set_window_floor);
    RUN_TEST(t_model_default_fallback);
    RUN_TEST(t_version_str_consistent);
    RUN_TEST(t_config_dirs_non_empty);
    RUN_TEST(t_buffer_macros_positive);
    RUN_TEST(t_buffer_macros_ordered);
    RUN_TEST(t_retry_macros_sane);
    RUN_TEST(t_live_compat_http_timeouts_sane);
    RUN_TEST(t_token_escalation_macros_sane);
    RUN_TEST(t_context_suggestion_macros);
    RUN_TEST(t_away_macros_sane);
    RUN_TEST(t_harness_macros_sane);
    RUN_TEST(t_subagent_delegation_macros);
    RUN_TEST(t_output_token_macros_ordered);
    RUN_TEST(t_env_var_macros_non_empty);
    RUN_TEST(t_net_err_macros_non_empty);
    RUN_TEST(t_cost_macros_positive);
    RUN_TEST(t_array_len_macro);
    RUN_TEST(t_hash_seed_macros);
    RUN_TEST(t_exit_code_macros);
    TEST_RETURN_FAILED();
}
