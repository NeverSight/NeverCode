#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_review_finding_init(void) {
    review_finding_t f;
    review_finding_init(&f);
    ASSERT_NULL(f.title);
    ASSERT_NULL(f.body);
    ASSERT_NULL(f.file_path);
    ASSERT_INT_EQ(f.start_line, 0);
    ASSERT_INT_EQ(f.end_line, 0);
    ASSERT_INT_EQ(f.severity, REVIEW_INFO);
    return 0;
}

static int test_review_finding_set(void) {
    review_finding_t f;
    review_finding_init(&f);
    int rc = review_finding_set(&f, "Missing null check", "ptr could be NULL here", "src/main.c",
                                42, 45, REVIEW_WARNING);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(f.title, "Missing null check");
    ASSERT_STR_EQ(f.body, "ptr could be NULL here");
    ASSERT_STR_EQ(f.file_path, "src/main.c");
    ASSERT_INT_EQ(f.start_line, 42);
    ASSERT_INT_EQ(f.end_line, 45);
    ASSERT_INT_EQ(f.severity, REVIEW_WARNING);
    review_finding_free(&f);
    return 0;
}

static int test_review_format_single(void) {
    review_finding_t f;
    review_finding_init(&f);
    review_finding_set(&f, "Buffer overflow", "Unbounded memcpy", "src/utils.c", 10, 12,
                       REVIEW_ERROR);

    char *out = review_format_findings(&f, 1, NULL);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "Review comment:") != NULL);
    ASSERT(strstr(out, "Buffer overflow") != NULL);
    ASSERT(strstr(out, "src/utils.c:10-12") != NULL);
    ASSERT(strstr(out, "error") != NULL);
    free(out);
    review_finding_free(&f);
    return 0;
}

static int test_review_format_multiple(void) {
    review_finding_t findings[2];
    review_finding_init(&findings[0]);
    review_finding_init(&findings[1]);
    review_finding_set(&findings[0], "Issue A", "Detail A", "a.c", 1, 5, REVIEW_INFO);
    review_finding_set(&findings[1], "Issue B", "Detail B", "b.c", 10, 20, REVIEW_CRITICAL);

    char *out = review_format_findings(findings, 2, NULL);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "Full review comments:") != NULL);
    ASSERT(strstr(out, "Issue A") != NULL);
    ASSERT(strstr(out, "Issue B") != NULL);
    ASSERT(strstr(out, "a.c:1-5") != NULL);
    ASSERT(strstr(out, "b.c:10-20") != NULL);
    free(out);
    review_findings_free(findings, 2);
    return 0;
}

static int test_review_format_with_selection(void) {
    review_finding_t findings[2];
    review_finding_init(&findings[0]);
    review_finding_init(&findings[1]);
    review_finding_set(&findings[0], "Fix A", NULL, "a.c", 1, 2, REVIEW_INFO);
    review_finding_set(&findings[1], "Fix B", NULL, "b.c", 3, 4, REVIEW_INFO);

    bool selection[] = {true, false};
    char *out = review_format_findings(findings, 2, selection);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "[x]") != NULL);
    ASSERT(strstr(out, "[ ]") != NULL);
    free(out);
    review_findings_free(findings, 2);
    return 0;
}

static int test_review_format_empty(void) {
    char *out = review_format_findings(NULL, 0, NULL);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "No review findings") != NULL);
    free(out);
    return 0;
}

static int test_review_summary_with_explanation(void) {
    review_finding_t f;
    review_finding_init(&f);
    review_finding_set(&f, "Bug", "Details", "x.c", 1, 1, REVIEW_ERROR);

    char *out = review_format_summary("Overall the code looks good.", &f, 1);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "Overall the code looks good.") != NULL);
    ASSERT(strstr(out, "Bug") != NULL);
    free(out);
    review_finding_free(&f);
    return 0;
}

static int test_review_summary_no_findings(void) {
    char *out = review_format_summary("Looks good!", NULL, 0);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "Looks good!") != NULL);
    free(out);
    return 0;
}

static int test_review_summary_empty(void) {
    char *out = review_format_summary("", NULL, 0);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "Reviewer failed") != NULL);
    free(out);
    return 0;
}

static int test_review_count_severity(void) {
    review_finding_t findings[3];
    review_finding_init(&findings[0]);
    review_finding_init(&findings[1]);
    review_finding_init(&findings[2]);
    review_finding_set(&findings[0], "A", NULL, NULL, 0, 0, REVIEW_WARNING);
    review_finding_set(&findings[1], "B", NULL, NULL, 0, 0, REVIEW_ERROR);
    review_finding_set(&findings[2], "C", NULL, NULL, 0, 0, REVIEW_WARNING);

    ASSERT_INT_EQ(review_count_by_severity(findings, 3, REVIEW_WARNING), 2);
    ASSERT_INT_EQ(review_count_by_severity(findings, 3, REVIEW_ERROR), 1);
    ASSERT_INT_EQ(review_count_by_severity(findings, 3, REVIEW_CRITICAL), 0);

    review_findings_free(findings, 3);
    return 0;
}

int test_review_format_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("review_format");
    RUN_TEST(test_review_finding_init);
    RUN_TEST(test_review_finding_set);
    RUN_TEST(test_review_format_single);
    RUN_TEST(test_review_format_multiple);
    RUN_TEST(test_review_format_with_selection);
    RUN_TEST(test_review_format_empty);
    RUN_TEST(test_review_summary_with_explanation);
    RUN_TEST(test_review_summary_no_findings);
    RUN_TEST(test_review_summary_empty);
    RUN_TEST(test_review_count_severity);
    TEST_RETURN_FAILED();
}
