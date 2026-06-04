#include "test.h"
#include "nevercode.h"

typedef struct api_error_t api_error_t;

void aerr_init(api_error_t *err);
int aerr_classify(api_error_t *err, int http_status, const char *body);
int aerr_classify_stream(api_error_t *err, const char *error_msg);
const char *aerr_kind_name(int kind);
int aerr_is_retryable(const api_error_t *err);
int aerr_has_error(const api_error_t *err);
char *aerr_format(const api_error_t *err);

static char err_buf[2048];

static int test_classify_200(void) {
    api_error_t *e = (api_error_t *)err_buf;
    int r = aerr_classify(e, 200, NULL);
    ASSERT_INT_EQ(r, 0);
    ASSERT_FALSE(aerr_has_error(e));
    return 0;
}

static int test_classify_429_rate(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 429, "{\"error\":{\"message\":\"too many requests\"}}");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_TRUE(aerr_is_retryable(e));
    ASSERT_STR_EQ(aerr_kind_name(3), "rate_limited"); /* AERR_RATE_LIMITED = 3 */
    return 0;
}

static int test_classify_429_quota(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 429, "{\"error\":{\"error_type\":\"usage_limit_reached\"}}");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_FALSE(aerr_is_retryable(e));
    return 0;
}

static int test_classify_400_context(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 400, "context_length_exceeded");
    ASSERT_TRUE(aerr_has_error(e));

    char *fmt = aerr_format(e);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "Context window");
    free(fmt);
    return 0;
}

static int test_classify_401(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 401, NULL);
    ASSERT_TRUE(aerr_has_error(e));

    char *fmt = aerr_format(e);
    ASSERT_STR_CONTAINS(fmt, "Authentication");
    free(fmt);
    return 0;
}

static int test_classify_503_overloaded(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 503, "server_is_overloaded");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_TRUE(aerr_is_retryable(e));
    return 0;
}

static int test_classify_stream(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify_stream(e, "connection timeout ETIMEDOUT");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_TRUE(aerr_is_retryable(e));
    return 0;
}

static int test_kind_names(void) {
    ASSERT_STR_EQ(aerr_kind_name(0), "none");
    ASSERT_STR_EQ(aerr_kind_name(1), "context_exceeded");
    ASSERT_STR_EQ(aerr_kind_name(4), "overloaded");
    ASSERT_STR_EQ(aerr_kind_name(99), "unknown");
    return 0;
}

static int test_format_none(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_init(e);
    ASSERT_NULL(aerr_format(e));
    return 0;
}

static int test_classify_400_invalid_image(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 400, "does not represent a valid image");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_FALSE(aerr_is_retryable(e));
    ASSERT_STR_EQ(aerr_kind_name(6), "invalid_image");
    return 0;
}

static int test_classify_400_generic(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 400, "some unknown 400 error");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_FALSE(aerr_is_retryable(e));
    ASSERT_STR_EQ(aerr_kind_name(5), "invalid_request");
    return 0;
}

static int test_classify_400_null_body(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 400, NULL);
    ASSERT_TRUE(aerr_has_error(e));
    char *fmt = aerr_format(e);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "no details");
    free(fmt);
    return 0;
}

static int test_classify_500(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 500, "internal error");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_TRUE(aerr_is_retryable(e));
    return 0;
}

static int test_classify_529_overloaded(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 529, "overloaded_error");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_TRUE(aerr_is_retryable(e));
    return 0;
}

static int test_classify_503_unavailable(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 503, "temporarily down for maintenance");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_TRUE(aerr_is_retryable(e));
    return 0;
}

static int test_classify_stream_overloaded(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify_stream(e, "overloaded_error from server");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_TRUE(aerr_is_retryable(e));
    return 0;
}

static int test_classify_stream_network(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify_stream(e, "ECONNREFUSED network failure");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_TRUE(aerr_is_retryable(e));
    return 0;
}

static int test_classify_stream_null(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify_stream(e, NULL);
    ASSERT_TRUE(aerr_has_error(e));
    return 0;
}

static int test_classify_unknown_status(void) {
    api_error_t *e = (api_error_t *)err_buf;
    aerr_classify(e, 418, "I'm a teapot");
    ASSERT_TRUE(aerr_has_error(e));
    ASSERT_FALSE(aerr_is_retryable(e));
    char *fmt = aerr_format(e);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "418");
    free(fmt);
    return 0;
}

int test_api_error_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("api_error");
    RUN_TEST(test_classify_200);
    RUN_TEST(test_classify_429_rate);
    RUN_TEST(test_classify_429_quota);
    RUN_TEST(test_classify_400_context);
    RUN_TEST(test_classify_400_invalid_image);
    RUN_TEST(test_classify_400_generic);
    RUN_TEST(test_classify_400_null_body);
    RUN_TEST(test_classify_401);
    RUN_TEST(test_classify_500);
    RUN_TEST(test_classify_503_overloaded);
    RUN_TEST(test_classify_503_unavailable);
    RUN_TEST(test_classify_529_overloaded);
    RUN_TEST(test_classify_stream);
    RUN_TEST(test_classify_stream_overloaded);
    RUN_TEST(test_classify_stream_network);
    RUN_TEST(test_classify_stream_null);
    RUN_TEST(test_classify_unknown_status);
    RUN_TEST(test_kind_names);
    RUN_TEST(test_format_none);
    return _t_failed;
}

#if 0
int main(void) { return test_api_error_all(); }
#endif
