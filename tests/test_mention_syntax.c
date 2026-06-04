#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int test_is_url_http(void) {
    ASSERT(mention_is_url("http://example.com") == true);
    ASSERT(mention_is_url("https://example.com") == true);
    return 0;
}

static int test_is_url_non_url(void) {
    ASSERT(mention_is_url("ftp://example.com") == false);
    ASSERT(mention_is_url("/path/to/file") == false);
    ASSERT(mention_is_url("file.txt") == false);
    ASSERT(mention_is_url("") == false);
    return 0;
}

static int test_parse_null(void) {
    char *r = mention_parse_enhanced(NULL);
    ASSERT_NULL(r);
    return 0;
}

static int test_parse_no_at(void) {
    char *r = mention_parse_enhanced("hello world");
    ASSERT_NULL(r);
    return 0;
}

static int test_parse_at_nonexistent(void) {
    char *r = mention_parse_enhanced("check @/nonexistent_path_9999");
    ASSERT_NULL(r);
    return 0;
}

static int test_parse_at_real_file(void) {
    FILE *f = fopen("/tmp/nevercode_test_mention.txt", "w");
    if (!f)
        return 0;
    fprintf(f, "test content here");
    fclose(f);

    char *r = mention_parse_enhanced("look at @/tmp/nevercode_test_mention.txt please");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "test content here") != NULL);
    ASSERT(strstr(r, "<file") != NULL);
    free(r);
    unlink("/tmp/nevercode_test_mention.txt");
    return 0;
}

static int test_parse_at_directory(void) {
    nc_mkdir_p("/tmp/nevercode_test_dir_mention");
    fclose(fopen("/tmp/nevercode_test_dir_mention/a.txt", "w"));
    fclose(fopen("/tmp/nevercode_test_dir_mention/b.c", "w"));

    char *r = mention_parse_enhanced("list @/tmp/nevercode_test_dir_mention/");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "<directory") != NULL);
    ASSERT(strstr(r, "a.txt") != NULL);
    ASSERT(strstr(r, "b.c") != NULL);
    free(r);

    nc_rm_rf("/tmp/nevercode_test_dir_mention");
    return 0;
}

static int test_count_refs_none(void) {
    ASSERT_INT_EQ(mention_count_refs("hello"), 0);
    ASSERT_INT_EQ(mention_count_refs(NULL), 0);
    return 0;
}

static int test_count_refs_file(void) {
    FILE *f = fopen("/tmp/nevercode_test_count.txt", "w");
    if (!f)
        return 0;
    fprintf(f, "x");
    fclose(f);

    int n = mention_count_refs("look at @/tmp/nevercode_test_count.txt");
    ASSERT(n >= 1);
    unlink("/tmp/nevercode_test_count.txt");
    return 0;
}

static int test_count_refs_url(void) {
    int n = mention_count_refs("check @https://example.com");
    ASSERT(n >= 1);
    return 0;
}

static int test_parse_multiple_refs(void) {
    FILE *f1 = fopen("/tmp/nevercode_multi_a.txt", "w");
    FILE *f2 = fopen("/tmp/nevercode_multi_b.txt", "w");
    if (!f1 || !f2) {
        if (f1)
            fclose(f1);
        if (f2)
            fclose(f2);
        return 0;
    }
    fprintf(f1, "aaa");
    fclose(f1);
    fprintf(f2, "bbb");
    fclose(f2);

    char *r = mention_parse_enhanced(
        "compare @/tmp/nevercode_multi_a.txt with @/tmp/nevercode_multi_b.txt");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "aaa") != NULL);
    ASSERT(strstr(r, "bbb") != NULL);
    free(r);

    unlink("/tmp/nevercode_multi_a.txt");
    unlink("/tmp/nevercode_multi_b.txt");
    return 0;
}

static int test_parse_line_range(void) {
    FILE *f = fopen("/tmp/nevercode_range_test.txt", "w");
    if (!f)
        return 0;
    for (int i = 1; i <= 20; i++)
        fprintf(f, "line %d content\n", i);
    fclose(f);

    char *r = mention_parse_enhanced("show @/tmp/nevercode_range_test.txt:5-10 only");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "line 5 content") != NULL);
    ASSERT(strstr(r, "line 10 content") != NULL);
    ASSERT(strstr(r, "line 1 content") == NULL);
    ASSERT(strstr(r, "line 11 content") == NULL);
    ASSERT(strstr(r, "lines=\"5-10\"") != NULL);
    free(r);
    unlink("/tmp/nevercode_range_test.txt");
    return 0;
}

static int test_parse_single_line(void) {
    FILE *f = fopen("/tmp/nevercode_single_line.txt", "w");
    if (!f)
        return 0;
    fprintf(f, "alpha\nbeta\ngamma\ndelta\n");
    fclose(f);

    char *r = mention_parse_enhanced("check @/tmp/nevercode_single_line.txt:3");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "gamma") != NULL);
    ASSERT(strstr(r, "alpha") == NULL);
    ASSERT(strstr(r, "lines=\"3-3\"") != NULL);
    free(r);
    unlink("/tmp/nevercode_single_line.txt");
    return 0;
}

int test_mention_syntax_all(void) {
    TEST_SUITE("mention_syntax");
    TEST_COUNTERS;

    RUN_TEST(test_is_url_http);
    RUN_TEST(test_is_url_non_url);
    RUN_TEST(test_parse_null);
    RUN_TEST(test_parse_no_at);
    RUN_TEST(test_parse_at_nonexistent);
    RUN_TEST(test_parse_at_real_file);
    RUN_TEST(test_parse_at_directory);
    RUN_TEST(test_count_refs_none);
    RUN_TEST(test_count_refs_file);
    RUN_TEST(test_count_refs_url);
    RUN_TEST(test_parse_multiple_refs);
    RUN_TEST(test_parse_line_range);
    RUN_TEST(test_parse_single_line);

    TEST_RETURN_FAILED();
}
