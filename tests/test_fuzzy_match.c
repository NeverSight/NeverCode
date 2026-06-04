#include "test.h"
#include "nevercode.h"

static int test_fuzzy_exact_match(void) {
    int score = 0;
    int rc = fuzzy_match("hello", "hello", NULL, 0, &score);
    ASSERT(rc == 5);
    ASSERT(score < 0);
    return 0;
}

static int test_fuzzy_subsequence(void) {
    int score = 0;
    int rc = fuzzy_match("hello world", "hlo", NULL, 0, &score);
    ASSERT(rc == 3);
    return 0;
}

static int test_fuzzy_case_insensitive(void) {
    int score = 0;
    int rc = fuzzy_match("FooBar", "foob", NULL, 0, &score);
    ASSERT(rc == 4);
    return 0;
}

static int test_fuzzy_no_match(void) {
    int rc = fuzzy_match("hello", "xyz", NULL, 0, NULL);
    ASSERT(rc == 0);
    return 0;
}

static int test_fuzzy_empty_needle(void) {
    int score = 0;
    int rc = fuzzy_match("anything", "", NULL, 0, &score);
    ASSERT(rc == 0);
    ASSERT(score == 0x7FFFFFFF);
    return 0;
}

static int test_fuzzy_indices(void) {
    int indices[8] = {0};
    int score = 0;
    int rc = fuzzy_match("hello", "hl", indices, 8, &score);
    ASSERT(rc == 2);
    ASSERT(indices[0] == 0);
    ASSERT(indices[1] == 2);
    return 0;
}

static int test_fuzzy_prefix_bonus(void) {
    int score_a = fuzzy_score("file_name", "file");
    int score_b = fuzzy_score("my_file_name", "file");
    ASSERT(score_a < score_b);
    return 0;
}

static int test_fuzzy_contiguous_bonus(void) {
    int score_a = fuzzy_score("abc", "abc");
    int score_b = fuzzy_score("a-b-c", "abc");
    ASSERT(score_a < score_b);
    return 0;
}

static int test_fuzzy_word_boundary(void) {
    int score_a = fuzzy_score("file_read", "fr");
    int score_b = fuzzy_score("filler", "fr");
    ASSERT(score_a <= score_b);
    return 0;
}

static int test_fuzzy_null_args(void) {
    ASSERT(fuzzy_match(NULL, "x", NULL, 0, NULL) == 0);
    ASSERT(fuzzy_match("x", NULL, NULL, 0, NULL) == 0);
    return 0;
}

static int test_fuzzy_filter_basic(void) {
    const char *items[] = {"Read", "Write", "Edit", "Grep", "Glob"};
    fuzzy_result_t results[8];
    int n = fuzzy_filter(items, 5, "re", results, 8);
    ASSERT(n >= 1);
    return 0;
}

static int test_fuzzy_filter_no_match(void) {
    const char *items[] = {"Read", "Write"};
    fuzzy_result_t results[4];
    int n = fuzzy_filter(items, 2, "xyz", results, 4);
    ASSERT(n == 0);
    return 0;
}

static int test_fuzzy_filter_sorted(void) {
    const char *items[] = {"my_long_read", "Read", "reader"};
    fuzzy_result_t results[8];
    int n = fuzzy_filter(items, 3, "read", results, 8);
    ASSERT(n >= 2);
    ASSERT(results[0].score <= results[1].score);
    return 0;
}

int test_fuzzy_match_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("fuzzy_match");

    RUN_TEST(test_fuzzy_exact_match);
    RUN_TEST(test_fuzzy_subsequence);
    RUN_TEST(test_fuzzy_case_insensitive);
    RUN_TEST(test_fuzzy_no_match);
    RUN_TEST(test_fuzzy_empty_needle);
    RUN_TEST(test_fuzzy_indices);
    RUN_TEST(test_fuzzy_prefix_bonus);
    RUN_TEST(test_fuzzy_contiguous_bonus);
    RUN_TEST(test_fuzzy_word_boundary);
    RUN_TEST(test_fuzzy_null_args);
    RUN_TEST(test_fuzzy_filter_basic);
    RUN_TEST(test_fuzzy_filter_no_match);
    RUN_TEST(test_fuzzy_filter_sorted);

    TEST_RETURN_FAILED();
}
