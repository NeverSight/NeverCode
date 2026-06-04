#include "test.h"
#include "nevercode.h"
#include <string.h>

static int t_short_message(void) {
    char *t = session_title_extract("Fix the login bug");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t, "Fix the login bug");
    free(t);
    return 0;
}

static int t_strips_prefix(void) {
    char *t = session_title_extract("Please fix the login bug");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t, "Fix the login bug");
    free(t);
    return 0;
}

static int t_strips_can_you(void) {
    char *t = session_title_extract("Can you refactor the parser?");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t, "Refactor the parser");
    free(t);
    return 0;
}

static int t_sentence_boundary(void) {
    char *t = session_title_extract("Add user auth. Then add tests for it.");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t, "Add user auth");
    free(t);
    return 0;
}

static int t_newline_boundary(void) {
    char *t = session_title_extract("Deploy to prod\nMake sure tests pass first");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t, "Deploy to prod");
    free(t);
    return 0;
}

static int t_word_limit(void) {
    char *t = session_title_extract("one two three four five six seven eight nine ten eleven");
    ASSERT_NOT_NULL(t);
    ASSERT(strstr(t, "nine") == NULL);
    free(t);
    return 0;
}

static int t_capitalizes(void) {
    char *t = session_title_extract("add a readme file");
    ASSERT_NOT_NULL(t);
    ASSERT(t[0] == 'A');
    free(t);
    return 0;
}

static int t_null_input(void) {
    ASSERT(session_title_extract(NULL) == NULL);
    ASSERT(session_title_extract("") == NULL);
    return 0;
}

static int t_whitespace_only(void) {
    ASSERT(session_title_extract("   \n\t  ") == NULL);
    return 0;
}

static int t_leading_whitespace(void) {
    char *t = session_title_extract("   hello world  ");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t, "Hello world");
    free(t);
    return 0;
}

int test_session_title_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_title");
    RUN_TEST(t_short_message);
    RUN_TEST(t_strips_prefix);
    RUN_TEST(t_strips_can_you);
    RUN_TEST(t_sentence_boundary);
    RUN_TEST(t_newline_boundary);
    RUN_TEST(t_word_limit);
    RUN_TEST(t_capitalizes);
    RUN_TEST(t_null_input);
    RUN_TEST(t_whitespace_only);
    RUN_TEST(t_leading_whitespace);
    TEST_RETURN_FAILED();
}
