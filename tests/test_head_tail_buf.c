#include "test.h"
#include "nevercode.h"

static int test_htbuf_init(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 100);
    ASSERT(htbuf_retained(&b) == 0);
    ASSERT(htbuf_omitted(&b) == 0);
    htbuf_free(&b);
    return 0;
}

static int test_htbuf_small_data(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 100);
    htbuf_push(&b, "hello", 5);
    ASSERT(htbuf_retained(&b) == 5);
    ASSERT(htbuf_omitted(&b) == 0);
    char *out = htbuf_finalize(&b);
    ASSERT_STR_EQ(out, "hello");
    free(out);
    htbuf_free(&b);
    return 0;
}

static int test_htbuf_exact_capacity(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 10);
    htbuf_push(&b, "12345", 5); /* fills head (budget=5) */
    htbuf_push(&b, "67890", 5); /* fills tail (budget=5) */
    ASSERT(htbuf_retained(&b) == 10);
    ASSERT(htbuf_omitted(&b) == 0);
    char *out = htbuf_finalize(&b);
    ASSERT_STR_EQ(out, "1234567890");
    free(out);
    htbuf_free(&b);
    return 0;
}

static int test_htbuf_overflow(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 10);
    htbuf_push(&b, "AAAAA", 5); /* head full */
    htbuf_push(&b, "BBBBB", 5); /* tail full */
    htbuf_push(&b, "CCCCC", 5); /* overflow: tail drops B, keeps C */
    ASSERT(htbuf_retained(&b) == 10);
    ASSERT(htbuf_omitted(&b) == 5);
    char *out = htbuf_finalize(&b);
    ASSERT_NOT_NULL(out);
    ASSERT(nc_starts_with(out, "AAAAA"));
    ASSERT(strstr(out, "omitted") != NULL);
    ASSERT(strstr(out, "CCCCC") != NULL);
    free(out);
    htbuf_free(&b);
    return 0;
}

static int test_htbuf_zero_capacity(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 0);
    htbuf_push(&b, "data", 4);
    ASSERT(htbuf_retained(&b) == 0);
    ASSERT(htbuf_omitted(&b) == 4);
    char *out = htbuf_finalize(&b);
    ASSERT_STR_EQ(out, "");
    free(out);
    htbuf_free(&b);
    return 0;
}

static int test_htbuf_null(void) {
    ASSERT(htbuf_retained(NULL) == 0);
    ASSERT(htbuf_omitted(NULL) == 0);
    char *out = htbuf_finalize(NULL);
    ASSERT_STR_EQ(out, "");
    free(out);
    return 0;
}

static int test_htbuf_incremental(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 20);
    for (int i = 0; i < 10; i++) {
        htbuf_push(&b, "AB", 2); /* total 20 bytes */
    }
    ASSERT(htbuf_retained(&b) == 20);
    ASSERT(htbuf_omitted(&b) == 0);
    char *out = htbuf_finalize(&b);
    ASSERT(strlen(out) == 20);
    free(out);
    htbuf_free(&b);
    return 0;
}

static int test_htbuf_with_meta(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 10);
    htbuf_push(&b, "AAAAAABBBBBBCCCCCC", 18); /* 5 head, 5 tail, 8 omitted */
    size_t total, omitted;
    char *out = htbuf_finalize_with_meta(&b, &total, &omitted);
    ASSERT(total == 18);
    ASSERT(omitted > 0);
    ASSERT_NOT_NULL(out);
    free(out);
    htbuf_free(&b);
    return 0;
}

int test_head_tail_buf_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("head_tail_buffer");
    RUN_TEST(test_htbuf_init);
    RUN_TEST(test_htbuf_small_data);
    RUN_TEST(test_htbuf_exact_capacity);
    RUN_TEST(test_htbuf_overflow);
    RUN_TEST(test_htbuf_zero_capacity);
    RUN_TEST(test_htbuf_null);
    RUN_TEST(test_htbuf_incremental);
    RUN_TEST(test_htbuf_with_meta);
    TEST_RETURN_FAILED();
}
