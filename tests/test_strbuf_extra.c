#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <string.h>

static int test_appendf_basic(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "hello %s", "world");
    ASSERT_STR_EQ(sb.data, "hello world");
    strbuf_free(&sb);
    return 0;
}

static int test_appendf_int(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "count: %d", 42);
    ASSERT_STR_EQ(sb.data, "count: 42");
    strbuf_free(&sb);
    return 0;
}

static int test_appendf_chained(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "a=%d ", 1);
    strbuf_appendf(&sb, "b=%d ", 2);
    strbuf_appendf(&sb, "c=%d", 3);
    ASSERT_STR_EQ(sb.data, "a=1 b=2 c=3");
    strbuf_free(&sb);
    return 0;
}

static int test_appendf_mixed_with_str(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "prefix:");
    strbuf_appendf(&sb, " %s=%d", "key", 99);
    ASSERT_STR_EQ(sb.data, "prefix: key=99");
    strbuf_free(&sb);
    return 0;
}

static int test_appendf_long_string(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    char expected[512];
    snprintf(expected, sizeof(expected), "path: %s/very/long/path/to/some/file.txt", "/home/user");
    strbuf_appendf(&sb, "path: %s/very/long/path/to/some/file.txt", "/home/user");
    ASSERT_STR_EQ(sb.data, expected);
    strbuf_free(&sb);
    return 0;
}

static int test_appendf_empty_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "%s", "");
    ASSERT(sb.len == 0 || (sb.data && sb.data[0] == '\0'));
    strbuf_free(&sb);
    return 0;
}

static int test_appendf_multiple_args(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "%s: %s (%d/%d)", "Status", "running", 3, 10);
    ASSERT_STR_EQ(sb.data, "Status: running (3/10)");
    strbuf_free(&sb);
    return 0;
}

int test_strbuf_extra_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("strbuf_extra");

    RUN_TEST(test_appendf_basic);
    RUN_TEST(test_appendf_int);
    RUN_TEST(test_appendf_chained);
    RUN_TEST(test_appendf_mixed_with_str);
    RUN_TEST(test_appendf_long_string);
    RUN_TEST(test_appendf_empty_format);
    RUN_TEST(test_appendf_multiple_args);

    TEST_RETURN_FAILED();
}
