#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

/* Declarations from request_compress.c */
extern void request_compress_init(void);
extern void request_compress_set_enabled(bool on);
extern bool request_compress_enabled(void);
extern void request_compress_set_threshold(int bytes);
extern void request_compress_set_level(int level);
extern int request_compress_gzip(const char *input, size_t input_len, char **output,
                                 size_t *output_len);
extern bool request_compress_should_compress(size_t payload_size);
extern int request_compress_maybe(const char *body, size_t body_len, char **out_body,
                                  size_t *out_len, bool *was_compressed);
extern double request_compress_ratio(void);
extern long request_compress_saved_bytes(void);
extern int request_compress_call_count(void);
extern int request_compress_skip_count(void);
extern char *request_compress_format(void);

static int test_init_state(void) {
    request_compress_init();
    ASSERT_TRUE(request_compress_enabled());
    ASSERT_INT_EQ(0, request_compress_call_count());
    ASSERT_INT_EQ(0, request_compress_skip_count());
    return 0;
}

static int test_enable_disable(void) {
    request_compress_init();
    request_compress_set_enabled(false);
    ASSERT_FALSE(request_compress_enabled());
    request_compress_set_enabled(true);
    ASSERT_TRUE(request_compress_enabled());
    return 0;
}

static int test_threshold(void) {
    request_compress_init();
    request_compress_set_threshold(100);
    ASSERT_TRUE(request_compress_should_compress(200));
    ASSERT_FALSE(request_compress_should_compress(50));
    return 0;
}

static int test_gzip_roundtrip(void) {
    request_compress_init();
    const char *data = "Hello, this is test data that should be compressible "
                       "if repeated enough. Hello, this is test data that "
                       "should be compressible if repeated enough.";
    size_t len = strlen(data);
    char *compressed = NULL;
    size_t clen = 0;

    int rc = request_compress_gzip(data, len, &compressed, &clen);
    ASSERT_INT_EQ(0, rc);
    ASSERT_NOT_NULL(compressed);
    ASSERT_GT((int)len, (int)clen);
    ASSERT_INT_EQ(1, request_compress_call_count());

    free(compressed);
    return 0;
}

static int test_maybe_skip_small(void) {
    request_compress_init();
    request_compress_set_threshold(1000);

    const char *small = "tiny";
    char *out = NULL;
    size_t out_len = 0;
    bool compressed = false;

    int rc = request_compress_maybe(small, strlen(small), &out, &out_len, &compressed);
    ASSERT_INT_EQ(0, rc);
    ASSERT_FALSE(compressed);
    ASSERT_NULL(out);
    ASSERT_INT_EQ(1, request_compress_skip_count());
    return 0;
}

static int test_format(void) {
    request_compress_init();
    char *fmt = request_compress_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "Compression");
    free(fmt);
    return 0;
}

int test_request_compress_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("request_compress");
    RUN_TEST(test_init_state);
    RUN_TEST(test_enable_disable);
    RUN_TEST(test_threshold);
    RUN_TEST(test_gzip_roundtrip);
    RUN_TEST(test_maybe_skip_small);
    RUN_TEST(test_format);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("request_compress");
    RUN_TEST(test_init_state);
    RUN_TEST(test_enable_disable);
    RUN_TEST(test_threshold);
    RUN_TEST(test_gzip_roundtrip);
    RUN_TEST(test_maybe_skip_small);
    RUN_TEST(test_format);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
