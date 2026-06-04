#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_approx_token_count_basic(void) {
    ASSERT_INT_EQ(approx_token_count(""), 0);
    ASSERT_INT_EQ(approx_token_count("hi"), 1);
    ASSERT_INT_EQ(approx_token_count("abcd"), 1);
    ASSERT_INT_EQ(approx_token_count("abcde"), 2);
    ASSERT(approx_token_count("hello world, this is a test") > 0);
    return 0;
}

static int test_approx_bytes_for_tokens(void) {
    ASSERT(approx_bytes_for_tokens(0) == 0);
    ASSERT(approx_bytes_for_tokens(1) == 4);
    ASSERT(approx_bytes_for_tokens(100) == 400);
    return 0;
}

static int test_middle_trunc_null(void) {
    ASSERT_NULL(middle_truncate_bytes(NULL, 100));
    ASSERT_NULL(middle_truncate_tokens(NULL, 100));
    return 0;
}

static int test_middle_trunc_short_string(void) {
    char *r = middle_truncate_bytes("hello", 100);
    ASSERT_STR_EQ(r, "hello");
    free(r);
    return 0;
}

static int test_middle_trunc_exact_boundary(void) {
    char *r = middle_truncate_bytes("12345", 5);
    ASSERT_STR_EQ(r, "12345");
    free(r);
    return 0;
}

static int test_middle_trunc_zero_budget(void) {
    char *r = middle_truncate_bytes("hello world", 0);
    ASSERT(r != NULL);
    ASSERT(strstr(r, "truncated") != NULL);
    free(r);
    return 0;
}

static int test_middle_trunc_basic(void) {
    char big[2048];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    char *r = middle_truncate_bytes(big, 200);
    ASSERT(r != NULL);
    ASSERT(strlen(r) < sizeof(big));
    ASSERT(strstr(r, "truncated") != NULL);
    ASSERT(r[0] == 'A');
    ASSERT(r[strlen(r) - 1] == 'A');
    free(r);
    return 0;
}

static int test_middle_trunc_preserves_head_tail(void) {
    char big[1024];
    for (int i = 0; i < 1023; i++)
        big[i] = '0' + (i % 10);
    big[1023] = '\0';

    char *r = middle_truncate_bytes(big, 200);
    ASSERT(r != NULL);
    ASSERT(r[0] == '0');
    ASSERT(strstr(r, "truncated") != NULL);
    free(r);
    return 0;
}

static int test_middle_trunc_utf8_safety(void) {
    char buf[512];
    int pos = 0;
    for (int i = 0; i < 100; i++) {
        buf[pos++] = (char)0xC3;
        buf[pos++] = (char)0xA9;
    }
    buf[pos] = '\0';

    char *r = middle_truncate_bytes(buf, 50);
    ASSERT(r != NULL);
    ASSERT(strlen(r) > 0);
    ASSERT(strstr(r, "truncated") != NULL);
    ASSERT((unsigned char)r[0] == 0xC3);
    free(r);
    return 0;
}

static int test_middle_trunc_tokens(void) {
    char big[2048];
    memset(big, 'X', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    char *r = middle_truncate_tokens(big, 50);
    ASSERT(r != NULL);
    ASSERT(strstr(r, "tokens truncated") != NULL);
    ASSERT(strlen(r) < sizeof(big));
    free(r);
    return 0;
}

static int test_tool_result_truncate_middle_short(void) {
    char *r = tool_result_truncate_middle("short output", "test_tool", 0);
    ASSERT_STR_EQ(r, "short output");
    free(r);
    return 0;
}

static int test_tool_result_truncate_middle_large(void) {
    size_t sz = TOOL_RESULT_MAX_CHARS + 1000;
    char *big = malloc(sz + 1);
    memset(big, 'Z', sz);
    big[sz] = '\0';

    char *r = tool_result_truncate_middle(big, "big_tool", 0);
    ASSERT(r != NULL);
    ASSERT(strlen(r) < sz);
    ASSERT(strstr(r, "Total output lines:") != NULL);
    ASSERT(strstr(r, "truncated") != NULL);
    free(r);
    free(big);
    return 0;
}

static int test_tool_result_truncate_middle_with_tokens(void) {
    size_t sz = 2000;
    char *big = malloc(sz + 1);
    memset(big, 'W', sz);
    big[sz] = '\0';

    char *r = tool_result_truncate_middle(big, "token_tool", 100);
    ASSERT(r != NULL);
    ASSERT(strstr(r, "truncated") != NULL);
    free(r);
    free(big);
    return 0;
}

static int test_line_count_in_truncation(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < 500; i++) {
        char line[64];
        snprintf(line, sizeof(line), "line %d content here\n", i);
        strbuf_append_str(&sb, line);
    }

    char *r = middle_truncate_bytes(sb.data, 200);
    ASSERT(r != NULL);
    ASSERT(strstr(r, "500 lines") != NULL);
    free(r);
    strbuf_free(&sb);
    return 0;
}

int test_middle_trunc_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("middle_trunc");
    RUN_TEST(test_approx_token_count_basic);
    RUN_TEST(test_approx_bytes_for_tokens);
    RUN_TEST(test_middle_trunc_null);
    RUN_TEST(test_middle_trunc_short_string);
    RUN_TEST(test_middle_trunc_exact_boundary);
    RUN_TEST(test_middle_trunc_zero_budget);
    RUN_TEST(test_middle_trunc_basic);
    RUN_TEST(test_middle_trunc_preserves_head_tail);
    RUN_TEST(test_middle_trunc_utf8_safety);
    RUN_TEST(test_middle_trunc_tokens);
    RUN_TEST(test_tool_result_truncate_middle_short);
    RUN_TEST(test_tool_result_truncate_middle_large);
    RUN_TEST(test_tool_result_truncate_middle_with_tokens);
    RUN_TEST(test_line_count_in_truncation);
    TEST_RETURN_FAILED();
}
