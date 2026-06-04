#include "test.h"
#include "nevercode.h"
#include "repl.h"
#include "terminal.h"

static int test_err_context_overflow(void) {
    ASSERT_INT_EQ(classify_api_error("context_length_exceeded"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("prompt is too long"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("too many tokens for model"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("reduce the length of messages"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("context window exceeded"), API_ERR_CONTEXT_OVERFLOW);
    ASSERT_INT_EQ(classify_api_error("maximum token limit"), API_ERR_CONTEXT_OVERFLOW);
    return 0;
}

static int test_err_rate_limit(void) {
    ASSERT_INT_EQ(classify_api_error("rate_limit_exceeded"), API_ERR_RATE_LIMIT);
    ASSERT_INT_EQ(classify_api_error("429 Too Many Requests"), API_ERR_RATE_LIMIT);
    ASSERT_INT_EQ(classify_api_error("quota exceeded"), API_ERR_RATE_LIMIT);
    ASSERT_INT_EQ(classify_api_error("request throttled"), API_ERR_RATE_LIMIT);
    return 0;
}

static int test_err_overloaded(void) {
    ASSERT_INT_EQ(classify_api_error("server overloaded"), API_ERR_OVERLOADED);
    ASSERT_INT_EQ(classify_api_error("503 Service Unavailable"), API_ERR_OVERLOADED);
    ASSERT_INT_EQ(classify_api_error("529"), API_ERR_OVERLOADED);
    ASSERT_INT_EQ(classify_api_error("temporarily unavailable"), API_ERR_OVERLOADED);
    ASSERT_INT_EQ(classify_api_error("system busy"), API_ERR_OVERLOADED);
    return 0;
}

static int test_err_auth(void) {
    ASSERT_INT_EQ(classify_api_error("401 Unauthorized"), API_ERR_AUTH);
    ASSERT_INT_EQ(classify_api_error("403 Forbidden"), API_ERR_AUTH);
    ASSERT_INT_EQ(classify_api_error("invalid_api_key"), API_ERR_AUTH);
    ASSERT_INT_EQ(classify_api_error("authentication failed"), API_ERR_AUTH);
    return 0;
}

static int test_err_watchdog(void) {
    ASSERT_INT_EQ(classify_api_error("turn deadline exceeded after wall-clock deadline"),
                  API_ERR_WATCHDOG);
    ASSERT_INT_EQ(
        classify_api_error("Multiple consecutive turns have timed out. The session context may be "
                           "degraded."),
        API_ERR_WATCHDOG);
    return 0;
}

static int test_err_network(void) {
    ASSERT_INT_EQ(classify_api_error("CURLE_COULDNT_RESOLVE"), API_ERR_NETWORK);
    ASSERT_INT_EQ(classify_api_error("Could not resolve host"), API_ERR_NETWORK);
    ASSERT_INT_EQ(classify_api_error("Connection refused"), API_ERR_NETWORK);
    ASSERT_INT_EQ(classify_api_error("request timed out"), API_ERR_NETWORK);
    ASSERT_INT_EQ(classify_api_error("SSL certificate problem"), API_ERR_NETWORK);
    return 0;
}

static int test_err_model_not_found(void) {
    ASSERT_INT_EQ(classify_api_error("model_not_found"), API_ERR_MODEL_NOT_FOUND);
    ASSERT_INT_EQ(classify_api_error("model does not exist"), API_ERR_MODEL_NOT_FOUND);
    ASSERT_INT_EQ(classify_api_error("model not available"), API_ERR_MODEL_NOT_FOUND);
    return 0;
}

static int test_err_invalid_request(void) {
    ASSERT_INT_EQ(classify_api_error("400 bad request"), API_ERR_INVALID_REQUEST);
    ASSERT_INT_EQ(classify_api_error("malformed JSON"), API_ERR_INVALID_REQUEST);
    return 0;
}

static int test_err_unknown(void) {
    ASSERT_INT_EQ(classify_api_error(NULL), API_ERR_UNKNOWN);
    ASSERT_INT_EQ(classify_api_error("some random error"), API_ERR_UNKNOWN);
    ASSERT_INT_EQ(classify_api_error(""), API_ERR_UNKNOWN);
    return 0;
}

static int test_err_tab_completion_cmds(void) {
    const char **cmds = term_get_slash_cmds();
    ASSERT_NOT_NULL(cmds);

    int count = 0;
    int has_status = 0, has_security = 0, has_diff = 0;
    int has_compact = 0, has_help = 0;

    for (int i = 0; cmds[i]; i++) {
        count++;
        if (strcmp(cmds[i], "/status") == 0)
            has_status = 1;
        if (strcmp(cmds[i], "/security-review") == 0)
            has_security = 1;
        if (strcmp(cmds[i], "/diff") == 0)
            has_diff = 1;
        if (strcmp(cmds[i], "/compact") == 0)
            has_compact = 1;
        if (strcmp(cmds[i], "/help") == 0)
            has_help = 1;
    }

    ASSERT(count >= 30);
    ASSERT(has_status == 1);
    ASSERT(has_security == 1);
    ASSERT(has_diff == 1);
    ASSERT(has_compact == 1);
    ASSERT(has_help == 1);
    return 0;
}

static int test_err_tab_cmds_sorted(void) {
    const char **cmds = term_get_slash_cmds();
    ASSERT_NOT_NULL(cmds);
    for (int i = 0; cmds[i] && cmds[i + 1]; i++) {
        ASSERT(strcmp(cmds[i], cmds[i + 1]) < 0);
    }
    return 0;
}

int test_error_classify_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("error_classify");
    RUN_TEST(test_err_context_overflow);
    RUN_TEST(test_err_rate_limit);
    RUN_TEST(test_err_overloaded);
    RUN_TEST(test_err_auth);
    RUN_TEST(test_err_watchdog);
    RUN_TEST(test_err_network);
    RUN_TEST(test_err_model_not_found);
    RUN_TEST(test_err_invalid_request);
    RUN_TEST(test_err_unknown);
    RUN_TEST(test_err_tab_completion_cmds);
    RUN_TEST(test_err_tab_cmds_sorted);
    TEST_RETURN_FAILED();
}
