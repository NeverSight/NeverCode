/*
 * test_xstrdup.c — Regression tests for nc_xstrdup / nc_xstrndup
 */

#include "test.h"
#include "nevercode.h"

static int test_xstrdup_basic(void) {
    char *s = nc_xstrdup("hello");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hello");
    free(s);
    return 0;
}

static int test_xstrdup_null(void) {
    char *s = nc_xstrdup(NULL);
    ASSERT_NULL(s);
    return 0;
}

static int test_xstrdup_empty(void) {
    char *s = nc_xstrdup("");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "");
    ASSERT_INT_EQ((int)strlen(s), 0);
    free(s);
    return 0;
}

static int test_xstrdup_independence(void) {
    const char *src = "original";
    char *dup = nc_xstrdup(src);
    ASSERT_NOT_NULL(dup);
    ASSERT_STR_EQ(dup, "original");
    dup[0] = 'X';
    ASSERT_STR_EQ(src, "original");
    ASSERT_STR_EQ(dup, "Xriginal");
    free(dup);
    return 0;
}

static int test_xstrndup_basic(void) {
    char *s = nc_xstrndup("hello world", 5);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hello");
    ASSERT_INT_EQ((int)strlen(s), 5);
    free(s);
    return 0;
}

static int test_xstrndup_null(void) {
    char *s = nc_xstrndup(NULL, 10);
    ASSERT_NULL(s);
    return 0;
}

static int test_xstrndup_zero_len(void) {
    char *s = nc_xstrndup("hello", 0);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "");
    free(s);
    return 0;
}

static int test_xstrndup_exceeds_src(void) {
    char *s = nc_xstrndup("hi", 100);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hi");
    ASSERT_INT_EQ((int)strlen(s), 2);
    free(s);
    return 0;
}

static int test_xstrndup_exact_len(void) {
    char *s = nc_xstrndup("abc", 3);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "abc");
    free(s);
    return 0;
}

int test_xstrdup_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("xstrdup");
    RUN_TEST(test_xstrdup_basic);
    RUN_TEST(test_xstrdup_null);
    RUN_TEST(test_xstrdup_empty);
    RUN_TEST(test_xstrdup_independence);
    RUN_TEST(test_xstrndup_basic);
    RUN_TEST(test_xstrndup_null);
    RUN_TEST(test_xstrndup_zero_len);
    RUN_TEST(test_xstrndup_exceeds_src);
    RUN_TEST(test_xstrndup_exact_len);
    TEST_RETURN_FAILED();
}
