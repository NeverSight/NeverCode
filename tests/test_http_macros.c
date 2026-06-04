#include "test.h"
#include "nevercode.h"

static int test_http_str_macros(void) {
    ASSERT_STR_EQ(HTTP_STR_400, "400");
    ASSERT_STR_EQ(HTTP_STR_401, "401");
    ASSERT_STR_EQ(HTTP_STR_403, "403");
    ASSERT_STR_EQ(HTTP_STR_429, "429");
    ASSERT_STR_EQ(HTTP_STR_500, "500");
    ASSERT_STR_EQ(HTTP_STR_502, "502");
    ASSERT_STR_EQ(HTTP_STR_503, "503");
    ASSERT_STR_EQ(HTTP_STR_529, "529");
    return 0;
}

static int test_http_err_macros(void) {
    ASSERT_STR_EQ(HTTP_ERR_RATE_LIMIT, "rate_limit");
    ASSERT_STR_EQ(HTTP_ERR_RATE_LIMIT_SP, "rate limit");
    ASSERT_STR_EQ(HTTP_ERR_OVERLOADED, "overloaded");
    ASSERT_STR_EQ(HTTP_ERR_UNAUTHORIZED, "unauthorized");
    ASSERT_STR_EQ(HTTP_ERR_FORBIDDEN, "forbidden");
    ASSERT_STR_EQ(HTTP_ERR_UNAVAILABLE, "Service Unavailable");
    ASSERT_STR_EQ(HTTP_ERR_INVALID, "invalid");
    ASSERT_STR_EQ(HTTP_ERR_BAD_REQUEST, "bad request");
    return 0;
}

static int test_http_context_macros(void) {
    ASSERT_STR_EQ(HTTP_ERR_CONTEXT_LENGTH, "context_length");
    ASSERT_STR_EQ(HTTP_ERR_MAX_TOKENS, "max_tokens");
    ASSERT_STR_EQ(HTTP_ERR_TOKEN_LIMIT, "token limit");
    return 0;
}

static int test_http_str_in_error_match(void) {
    const char *msg = "API returned 429: rate_limit exceeded";
    ASSERT_TRUE(strstr(msg, HTTP_STR_429) != NULL);
    ASSERT_TRUE(strstr(msg, HTTP_ERR_RATE_LIMIT) != NULL);
    ASSERT_TRUE(strstr(msg, HTTP_STR_503) == NULL);
    return 0;
}

static int test_dashboard_macros(void) {
    ASSERT_TRUE(DASHBOARD_BAR_WIDTH > 0);
    ASSERT_TRUE(DASHBOARD_PCT_WIDTH > 0);
    ASSERT_TRUE(DASHBOARD_LABEL_WIDTH > 0);
    return 0;
}

static int test_collab_followup_macros(void) {
    ASSERT_INT_EQ(COLLAB_FOLLOWUP_TRIGGER_TURN, 1);
    ASSERT_INT_EQ(COLLAB_FOLLOWUP_QUEUE_ONLY, 0);
    return 0;
}

static int test_media_limit_macros(void) {
    ASSERT_TRUE(MEDIA_IMAGE_MAX_B64 == 5 * 1024 * 1024);
    ASSERT_TRUE(MEDIA_IMAGE_RAW_MAX < MEDIA_IMAGE_MAX_B64);
    ASSERT_INT_EQ(MEDIA_IMAGE_MAX_DIM, 2000);
    ASSERT_TRUE(MEDIA_PDF_RAW_MAX == 20 * 1024 * 1024);
    ASSERT_INT_EQ(MEDIA_PDF_MAX_PAGES, 100);
    ASSERT_INT_EQ(MEDIA_PER_REQUEST, 100);
    return 0;
}

static int test_effort_glyph_macros(void) {
    ASSERT_NOT_NULL(EFFORT_GLYPH_NONE);
    ASSERT_NOT_NULL(EFFORT_GLYPH_LOW);
    ASSERT_NOT_NULL(EFFORT_GLYPH_MEDIUM);
    ASSERT_NOT_NULL(EFFORT_GLYPH_HIGH);
    ASSERT_NOT_NULL(EFFORT_GLYPH_MAX);
    ASSERT_TRUE(strlen(EFFORT_GLYPH_HIGH) > 0);
    return 0;
}

static int test_agent_disallowed_macro(void) {
    ASSERT_INT_EQ(AGENT_DISALLOWED_TOOLS_MAX, 16);
    ASSERT_TRUE(TOOL_RESULTS_PER_MSG_MAX == 200000);
    ASSERT_INT_EQ(TOOL_SUMMARY_TRUNC_MAX, 50);
    return 0;
}

int test_http_macros_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("http_macros");
    RUN_TEST(test_http_str_macros);
    RUN_TEST(test_http_err_macros);
    RUN_TEST(test_http_context_macros);
    RUN_TEST(test_http_str_in_error_match);
    RUN_TEST(test_dashboard_macros);
    RUN_TEST(test_collab_followup_macros);
    RUN_TEST(test_media_limit_macros);
    RUN_TEST(test_effort_glyph_macros);
    RUN_TEST(test_agent_disallowed_macro);
    return _t_failed;
}
