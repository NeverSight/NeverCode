#include "test.h"
#include "nevercode.h"
#include "defaults.h"

static int test_short_line(void) {
    char *r = line_wrap("hello", 80);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "hello");
    free(r);
    return 0;
}

static int test_wrap_at_space(void) {
    char *r = line_wrap("hello world foo bar", 11);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "\n");
    ASSERT_STR_CONTAINS(r, "hello");
    ASSERT_STR_CONTAINS(r, "world");
    free(r);
    return 0;
}

static int test_preserves_newlines(void) {
    char *r = line_wrap("line1\nline2\nline3", 80);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "line1\nline2\nline3");
    free(r);
    return 0;
}

static int test_ansi_transparent(void) {
    char *r = line_wrap("\033[31mred\033[0m text", 80);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "\033[31m");
    ASSERT_STR_CONTAINS(r, "red");
    free(r);
    return 0;
}

static int test_visible_width(void) {
    ASSERT_INT_EQ(line_visible_width("hello"), 5);
    ASSERT_INT_EQ(line_visible_width("\033[1mhello\033[0m"), 5);
    ASSERT_INT_EQ(line_visible_width(""), 0);
    ASSERT_INT_EQ(line_visible_width(NULL), 0);
    return 0;
}

static int test_visible_width_utf8(void) {
    ASSERT_INT_EQ(line_visible_width("\xe2\x94\x82"), 1);
    ASSERT_INT_EQ(line_visible_width("a\xe2\x96\xb6 b\xe2\x94\x82"
                                     "c"),
                  6);
    ASSERT_INT_EQ(line_visible_width("\xc3\xa9"), 1);
    ASSERT_INT_EQ(line_visible_width("caf\xc3\xa9"), 4);
    ASSERT_INT_EQ(line_visible_width("\xe4\xbd\xa0"), 2);
    ASSERT_INT_EQ(line_visible_width("\xe4\xbd\xa0"
                                     "a"),
                  3);
    return 0;
}

static int test_wrap_preserves_cjk_cell_width(void) {
    char *r = line_wrap("ab"
                        "\xe4\xbd\xa0"
                        "cd",
                        4);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "ab"
                     "\xe4\xbd\xa0"
                     "\ncd");
    free(r);
    return 0;
}

static int test_wrap_count(void) {
    ASSERT_INT_EQ(line_wrap_count("hello", 80), 1);
    ASSERT_INT_EQ(line_wrap_count("hello\nworld", 80), 2);
    ASSERT_GT(line_wrap_count("abcdef", 3), 1);
    return 0;
}

static int test_null_input(void) {
    ASSERT_NULL(line_wrap(NULL, 80));
    ASSERT_INT_EQ(line_wrap_count(NULL, 80), 0);
    return 0;
}

static int test_default_width(void) {
    char *r = line_wrap("short", 0);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "short");
    free(r);
    return 0;
}

int test_line_wrap_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("line_wrap");
    RUN_TEST(test_short_line);
    RUN_TEST(test_wrap_at_space);
    RUN_TEST(test_preserves_newlines);
    RUN_TEST(test_ansi_transparent);
    RUN_TEST(test_visible_width);
    RUN_TEST(test_visible_width_utf8);
    RUN_TEST(test_wrap_preserves_cjk_cell_width);
    RUN_TEST(test_wrap_count);
    RUN_TEST(test_null_input);
    RUN_TEST(test_default_width);
    TEST_RETURN_FAILED();
}
