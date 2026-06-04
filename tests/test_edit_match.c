#include "test.h"
#include "nevercode.h"
#include <string.h>

static int test_rstrip(void) {
    char *r = edit_rstrip_lines("hello   \nworld\t \n");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "hello\nworld\n");
    free(r);
    return 0;
}

static int test_trim(void) {
    char *r = edit_trim_lines("  hello   \n\tworld\t \n");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "hello\nworld\n");
    free(r);
    return 0;
}

static int test_collapse(void) {
    char *r = edit_collapse_ws("a   b\t\tc\n  d  e  \n");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "a b c\n d e \n");
    free(r);
    return 0;
}

static int test_strip_indent(void) {
    char *r = edit_strip_indent("    line1\n\tline2\n");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "line1\nline2\n");
    free(r);
    return 0;
}

static int test_unicode_norm(void) {
    /* \xe2\x80\x9c = left double quote, \xe2\x80\x9d = right double quote */
    char *r = edit_unicode_norm("\xe2\x80\x9chello\xe2\x80\x9d");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "\"hello\"");
    free(r);

    /* \xc2\xa0 = NBSP */
    r = edit_unicode_norm("a\xc2\xa0"
                          "b");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "a b");
    free(r);

    /* \xe2\x80\x94 = em-dash */
    r = edit_unicode_norm("a\xe2\x80\x94"
                          "b");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "a--b");
    free(r);

    return 0;
}

static int test_fuzzy_exact(void) {
    size_t off = 0, mlen = 0;
    int lvl = -1;
    bool found = edit_fuzzy_locate("hello world", "world", 5, &off, &mlen, &lvl);
    ASSERT_TRUE(found);
    ASSERT_INT_EQ((int)off, 6);
    ASSERT_INT_EQ((int)mlen, 5);
    ASSERT_INT_EQ(lvl, 0);
    return 0;
}

static int test_fuzzy_rstrip_match(void) {
    size_t off = 0, mlen = 0;
    int lvl = -1;
    bool found = edit_fuzzy_locate("line1  \nline2\n", "line1\nline2", 11, &off, &mlen, &lvl);
    ASSERT_TRUE(found);
    ASSERT_INT_EQ(lvl, 1);
    return 0;
}

static int test_fuzzy_indent_match(void) {
    size_t off = 0, mlen = 0;
    int lvl = -1;
    bool found = edit_fuzzy_locate("    foo\n      bar\n", "foo\nbar", 7, &off, &mlen, &lvl);
    ASSERT_TRUE(found);
    ASSERT_TRUE(lvl >= 2);
    return 0;
}

static int test_fuzzy_not_found(void) {
    size_t off = 0, mlen = 0;
    int lvl = -1;
    bool found = edit_fuzzy_locate("hello world", "xyz123", 6, &off, &mlen, &lvl);
    ASSERT_TRUE(!found);
    return 0;
}

static int test_fuzzy_count_exact(void) {
    int c = edit_fuzzy_count("aaa bbb aaa ccc aaa", "aaa", 3, 0);
    ASSERT_INT_EQ(c, 3);
    return 0;
}

static int test_level_names(void) {
    ASSERT_STR_EQ(edit_level_names[0], "exact");
    ASSERT_STR_EQ(edit_level_names[EDIT_FUZZY_MATCH_LEVELS - 1], "unicode-norm");
    return 0;
}

static int test_norm_fns_table(void) {
    ASSERT_TRUE(edit_norm_fns[0] == NULL);
    for (int i = 1; i < EDIT_FUZZY_MATCH_LEVELS; i++)
        ASSERT_NOT_NULL((void *)(uintptr_t)edit_norm_fns[i]);
    return 0;
}

int test_edit_match_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("edit_match");
    RUN_TEST(test_rstrip);
    RUN_TEST(test_trim);
    RUN_TEST(test_collapse);
    RUN_TEST(test_strip_indent);
    RUN_TEST(test_unicode_norm);
    RUN_TEST(test_fuzzy_exact);
    RUN_TEST(test_fuzzy_rstrip_match);
    RUN_TEST(test_fuzzy_indent_match);
    RUN_TEST(test_fuzzy_not_found);
    RUN_TEST(test_fuzzy_count_exact);
    RUN_TEST(test_level_names);
    RUN_TEST(test_norm_fns_table);
    TEST_RETURN_FAILED();
}
