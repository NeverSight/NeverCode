#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_review_prompt_basic(void) {
    char *p = build_review_prompt(NULL, NULL);
    ASSERT(p != NULL);
    ASSERT(strstr(p, "Security") != NULL);
    ASSERT(strstr(p, "Performance") != NULL);
    ASSERT(strstr(p, "Correctness") != NULL);
    ASSERT(strstr(p, "CRITICAL") != NULL);
    free(p);
    return 0;
}

static int test_review_prompt_with_target(void) {
    char *p = build_review_prompt("src/main.c", NULL);
    ASSERT(p != NULL);
    ASSERT(strstr(p, "src/main.c") != NULL);
    ASSERT(strstr(p, "Review Target") != NULL);
    free(p);
    return 0;
}

static int test_review_prompt_with_focus(void) {
    char *p = build_review_prompt(NULL, "memory safety");
    ASSERT(p != NULL);
    ASSERT(strstr(p, "memory safety") != NULL);
    ASSERT(strstr(p, "Review Focus") != NULL);
    free(p);
    return 0;
}

static int test_review_prompt_with_both(void) {
    char *p = build_review_prompt("lib/api.py", "SQL injection");
    ASSERT(p != NULL);
    ASSERT(strstr(p, "lib/api.py") != NULL);
    ASSERT(strstr(p, "SQL injection") != NULL);
    free(p);
    return 0;
}

static int test_security_review_prompt(void) {
    char *p = build_security_review_prompt("server/auth.js");
    ASSERT(p != NULL);
    ASSERT(strstr(p, "security") != NULL || strstr(p, "Security") != NULL);
    ASSERT(strstr(p, "Command injection") != NULL);
    ASSERT(strstr(p, "server/auth.js") != NULL);
    free(p);
    return 0;
}

static int test_security_review_prompt_null(void) {
    char *p = build_security_review_prompt(NULL);
    ASSERT(p != NULL);
    ASSERT(strstr(p, "XSS") != NULL);
    free(p);
    return 0;
}

static int test_review_parse_severity(void) {
    ASSERT_INT_EQ(review_parse_severity_tag("### [CRITICAL] Buffer overflow"), REVIEW_CRITICAL);
    ASSERT_INT_EQ(review_parse_severity_tag("### [ERROR] Null deref"), REVIEW_ERROR);
    ASSERT_INT_EQ(review_parse_severity_tag("### [WARNING] Unused var"), REVIEW_WARNING);
    ASSERT_INT_EQ(review_parse_severity_tag("### [INFO] Style note"), REVIEW_INFO);
    ASSERT_INT_EQ(review_parse_severity_tag("No severity here"), -1);
    ASSERT_INT_EQ(review_parse_severity_tag(NULL), -1);
    return 0;
}

static int test_review_parse_file_line(void) {
    char path[256];
    int start, end;

    int rc = review_parse_file_line("**File:** src/main.c (lines 10-20)", path, sizeof(path),
                                    &start, &end);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(path, "src/main.c");
    ASSERT_INT_EQ(start, 10);
    ASSERT_INT_EQ(end, 20);
    return 0;
}

static int test_review_parse_file_line_single(void) {
    char path[256];
    int start, end;

    int rc = review_parse_file_line("**File:** lib/utils.py (line 42)", path, sizeof(path), &start,
                                    &end);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(path, "lib/utils.py");
    ASSERT_INT_EQ(start, 42);
    return 0;
}

static int test_review_parse_file_no_lines(void) {
    char path[256];
    int start = -1, end = -1;

    int rc = review_parse_file_line("**File:** config.json", path, sizeof(path), &start, &end);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(path, "config.json");
    ASSERT_INT_EQ(start, 0);
    ASSERT_INT_EQ(end, 0);
    return 0;
}

static int test_review_parse_no_file_tag(void) {
    char path[256];
    int start, end;
    ASSERT(review_parse_file_line("just a line", path, sizeof(path), &start, &end) != 0);
    return 0;
}

int test_review_integration_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("review_integration");
    RUN_TEST(test_review_prompt_basic);
    RUN_TEST(test_review_prompt_with_target);
    RUN_TEST(test_review_prompt_with_focus);
    RUN_TEST(test_review_prompt_with_both);
    RUN_TEST(test_security_review_prompt);
    RUN_TEST(test_security_review_prompt_null);
    RUN_TEST(test_review_parse_severity);
    RUN_TEST(test_review_parse_file_line);
    RUN_TEST(test_review_parse_file_line_single);
    RUN_TEST(test_review_parse_file_no_lines);
    RUN_TEST(test_review_parse_no_file_tag);
    TEST_RETURN_FAILED();
}
