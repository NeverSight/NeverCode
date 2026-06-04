#include "test.h"
#include "nevercode.h"

char *rp_build_uncommitted(void);
char *rp_build_branch(const char *branch, const char *merge_base);
char *rp_build_commit(const char *sha, const char *title);
char *rp_build_file(const char *path);
char *rp_build_custom(const char *tpl, const char **keys, const char **values, int count);
const char *rp_target_name(int t);

static int test_uncommitted(void) {
    char *p = rp_build_uncommitted();
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "code changes");
    free(p);
    return 0;
}

static int test_branch_with_base(void) {
    char *p = rp_build_branch("main", "abc123");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "main");
    ASSERT_STR_CONTAINS(p, "abc123");
    ASSERT_STR_CONTAINS(p, "merge base");
    free(p);
    return 0;
}

static int test_branch_fallback(void) {
    char *p = rp_build_branch("develop", NULL);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "develop");
    ASSERT_STR_CONTAINS(p, "merge-base");
    free(p);
    return 0;
}

static int test_commit_with_title(void) {
    char *p = rp_build_commit("deadbeef", "Fix buffer overflow");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "deadbeef");
    ASSERT_STR_CONTAINS(p, "Fix buffer overflow");
    free(p);
    return 0;
}

static int test_commit_no_title(void) {
    char *p = rp_build_commit("cafe1234", NULL);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "cafe1234");
    free(p);
    return 0;
}

static int test_file(void) {
    char *p = rp_build_file("src/main.c");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "src/main.c");
    free(p);
    return 0;
}

static int test_custom_template(void) {
    const char *keys[] = {"name", "lang"};
    const char *vals[] = {"MyProject", "C"};
    char *p = rp_build_custom("Review {{name}} written in {{lang}}.", keys, vals, 2);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "MyProject");
    ASSERT_STR_CONTAINS(p, "written in C");
    free(p);
    return 0;
}

static int test_target_names(void) {
    ASSERT_STR_EQ(rp_target_name(0), "uncommitted");
    ASSERT_STR_EQ(rp_target_name(1), "branch");
    ASSERT_STR_EQ(rp_target_name(2), "commit");
    ASSERT_STR_EQ(rp_target_name(3), "file");
    ASSERT_STR_EQ(rp_target_name(4), "custom");
    return 0;
}

int test_review_prompt_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("review_prompt");
    RUN_TEST(test_uncommitted);
    RUN_TEST(test_branch_with_base);
    RUN_TEST(test_branch_fallback);
    RUN_TEST(test_commit_with_title);
    RUN_TEST(test_commit_no_title);
    RUN_TEST(test_file);
    RUN_TEST(test_custom_template);
    RUN_TEST(test_target_names);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("review_prompt");
    RUN_TEST(test_uncommitted);
    RUN_TEST(test_branch_with_base);
    RUN_TEST(test_branch_fallback);
    RUN_TEST(test_commit_with_title);
    RUN_TEST(test_commit_no_title);
    RUN_TEST(test_file);
    RUN_TEST(test_custom_template);
    RUN_TEST(test_target_names);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
