#include "test.h"
#include "nevercode.h"

typedef struct {
    char *request_id;
    char *cf_ray;
    char *auth_error;
    char *error_code;
} resp_debug_t;

void resp_debug_init(resp_debug_t *ctx);
void resp_debug_free(resp_debug_t *ctx);
int resp_debug_extract(resp_debug_t *ctx, int status_code, const char *headers, const char *body);
char *resp_debug_format(const resp_debug_t *ctx);
int resp_debug_is_auth_error(const resp_debug_t *ctx);
int resp_debug_has_info(const resp_debug_t *ctx);

static int test_extract_request_id(void) {
    resp_debug_t ctx;
    resp_debug_init(&ctx);

    const char *headers = "x-request-id: req-12345\r\ncf-ray: abc-def\r\n";
    int r = resp_debug_extract(&ctx, 429, headers, NULL);
    ASSERT_INT_EQ(r, 1);
    ASSERT_STR_EQ(ctx.request_id, "req-12345");
    ASSERT_STR_EQ(ctx.cf_ray, "abc-def");
    ASSERT_FALSE(resp_debug_is_auth_error(&ctx));
    ASSERT_TRUE(resp_debug_has_info(&ctx));

    resp_debug_free(&ctx);
    return 0;
}

static int test_extract_oai_request_id(void) {
    resp_debug_t ctx;
    resp_debug_init(&ctx);

    const char *headers = "x-oai-request-id: oai-99\r\n";
    resp_debug_extract(&ctx, 500, headers, NULL);
    ASSERT_STR_EQ(ctx.request_id, "oai-99");

    resp_debug_free(&ctx);
    return 0;
}

static int test_extract_auth_error(void) {
    resp_debug_t ctx;
    resp_debug_init(&ctx);

    const char *headers = "x-openai-authorization-error: invalid_key\r\n";
    resp_debug_extract(&ctx, 401, headers, NULL);
    ASSERT_TRUE(resp_debug_is_auth_error(&ctx));
    ASSERT_STR_EQ(ctx.auth_error, "invalid_key");

    resp_debug_free(&ctx);
    return 0;
}

static int test_format(void) {
    resp_debug_t ctx;
    resp_debug_init(&ctx);
    ctx.request_id = nc_xstrdup("req-1");
    ctx.cf_ray = nc_xstrdup("ray-2");

    char *s = resp_debug_format(&ctx);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Request-ID: req-1");
    ASSERT_STR_CONTAINS(s, "CF-Ray: ray-2");
    free(s);

    resp_debug_free(&ctx);
    return 0;
}

static int test_null_headers(void) {
    resp_debug_t ctx;
    resp_debug_init(&ctx);
    int r = resp_debug_extract(&ctx, 500, NULL, NULL);
    ASSERT_INT_EQ(r, 0);
    ASSERT_FALSE(resp_debug_has_info(&ctx));
    resp_debug_free(&ctx);
    return 0;
}

int test_resp_debug_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("resp_debug");
    RUN_TEST(test_extract_request_id);
    RUN_TEST(test_extract_oai_request_id);
    RUN_TEST(test_extract_auth_error);
    RUN_TEST(test_format);
    RUN_TEST(test_null_headers);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("resp_debug");
    RUN_TEST(test_extract_request_id);
    RUN_TEST(test_extract_oai_request_id);
    RUN_TEST(test_extract_auth_error);
    RUN_TEST(test_format);
    RUN_TEST(test_null_headers);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
