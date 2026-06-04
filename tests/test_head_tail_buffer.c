#include "test.h"
#include "nevercode.h"
#include <string.h>

static int test_init_free(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 1024);
    ASSERT_INT_EQ((int)htbuf_retained(&b), 0);
    ASSERT_INT_EQ((int)htbuf_omitted(&b), 0);
    htbuf_free(&b);
    return 0;
}

static int test_small_push(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 1024);
    htbuf_push(&b, "hello", 5);
    ASSERT_INT_EQ((int)htbuf_retained(&b), 5);
    ASSERT_INT_EQ((int)htbuf_omitted(&b), 0);
    char *out = htbuf_finalize(&b);
    ASSERT_STR_EQ(out, "hello");
    free(out);
    htbuf_free(&b);
    return 0;
}

static int test_multi_push(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 1024);
    htbuf_push(&b, "abc", 3);
    htbuf_push(&b, "def", 3);
    ASSERT_INT_EQ((int)htbuf_retained(&b), 6);
    char *out = htbuf_finalize(&b);
    ASSERT_STR_EQ(out, "abcdef");
    free(out);
    htbuf_free(&b);
    return 0;
}

static int test_overflow_omits_middle(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 20);
    char block[30];
    memset(block, 'A', 10);
    memset(block + 10, 'B', 10);
    memset(block + 20, 'C', 10);
    htbuf_push(&b, block, 30);
    ASSERT_INT_EQ((int)htbuf_retained(&b), 20);
    ASSERT_GT((int)htbuf_omitted(&b), 0);
    char *out = htbuf_finalize(&b);
    ASSERT_NOT_NULL(out);
    ASSERT_TRUE(strstr(out, "omitted") != NULL);
    free(out);
    htbuf_free(&b);
    return 0;
}

static int test_finalize_empty(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 100);
    char *out = htbuf_finalize(&b);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "");
    free(out);
    htbuf_free(&b);
    return 0;
}

static int test_null_safety(void) {
    htbuf_init(NULL, 100);
    htbuf_push(NULL, "x", 1);
    ASSERT_INT_EQ((int)htbuf_retained(NULL), 0);
    ASSERT_INT_EQ((int)htbuf_omitted(NULL), 0);
    char *out = htbuf_finalize(NULL);
    ASSERT_STR_EQ(out, "");
    free(out);
    htbuf_free(NULL);
    return 0;
}

static int test_finalize_with_meta(void) {
    head_tail_buf_t b;
    htbuf_init(&b, 100);
    htbuf_push(&b, "test data", 9);
    size_t total = 0, omitted = 0;
    char *out = htbuf_finalize_with_meta(&b, &total, &omitted);
    ASSERT_NOT_NULL(out);
    ASSERT_INT_EQ((int)total, 9);
    ASSERT_INT_EQ((int)omitted, 0);
    free(out);
    htbuf_free(&b);
    return 0;
}

int test_head_tail_buffer_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("head_tail_buffer");
    RUN_TEST(test_init_free);
    RUN_TEST(test_small_push);
    RUN_TEST(test_multi_push);
    RUN_TEST(test_overflow_omits_middle);
    RUN_TEST(test_finalize_empty);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_finalize_with_meta);
    TEST_RETURN_FAILED();
}
