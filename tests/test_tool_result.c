#include "test.h"
#include "nevercode.h"
#include <string.h>

static int test_truncate_short(void) {
    char *r = tool_result_truncate("hi", "t");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "hi");
    free(r);
    return 0;
}

static int test_truncate_null(void) {
    char *r = tool_result_truncate(NULL, "t");
    ASSERT_NULL(r);
    return 0;
}

static int test_truncate_exact_limit(void) {
    char small[256];
    memset(small, 'x', sizeof(small) - 1);
    small[sizeof(small) - 1] = '\0';
    char *r = tool_result_truncate(small, "test");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, small);
    free(r);
    return 0;
}

static int test_truncate_large(void) {
    size_t sz = TOOL_RESULT_MAX_CHARS + 1000;
    char *big = malloc(sz + 1);
    memset(big, 'Z', sz);
    big[sz] = '\0';
    char *r = tool_result_truncate(big, "bigtest");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "TRUNCATED");
    ASSERT_TRUE(strlen(r) < sz);
    free(r);
    free(big);
    return 0;
}

static int test_truncate_preserves_ends(void) {
    size_t sz = TOOL_RESULT_MAX_CHARS + 5000;
    char *big = malloc(sz + 1);
    memset(big, 'M', sz);
    big[0] = 'A';
    big[1] = 'B';
    big[sz - 2] = 'Y';
    big[sz - 1] = 'Z';
    big[sz] = '\0';
    char *r = tool_result_truncate(big, "ends");
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r[0] == 'A');
    ASSERT_TRUE(r[1] == 'B');
    size_t rl = strlen(r);
    ASSERT_TRUE(r[rl - 1] == 'Z');
    ASSERT_TRUE(r[rl - 2] == 'Y');
    free(r);
    free(big);
    return 0;
}

static int test_msg_budget_under(void) {
    const char *results[] = {"short", "tiny"};
    int lens[] = {5, 4};
    ASSERT_INT_EQ(trs_check_msg_budget(results, lens, 2, NULL), 0);
    return 0;
}

static int test_msg_budget_over(void) {
    char *big1 = malloc(120001);
    char *big2 = malloc(120001);
    memset(big1, 'A', 120000);
    big1[120000] = '\0';
    memset(big2, 'B', 120000);
    big2[120000] = '\0';
    const char *results[] = {big1, big2};
    int over = -1;
    ASSERT_INT_EQ(trs_check_msg_budget(results, NULL, 2, &over), 1);
    ASSERT_TRUE(over == 0 || over == 1);
    free(big1);
    free(big2);
    return 0;
}

static int test_msg_budget_null(void) {
    ASSERT_INT_EQ(trs_check_msg_budget(NULL, NULL, 0, NULL), 0);
    return 0;
}

static int test_cleanup_no_dir(void) {
    int removed = tool_result_cleanup();
    ASSERT_TRUE(removed >= 0);
    return 0;
}

static int test_cleanup_returns_count(void) {
    int r = tool_result_cleanup();
    ASSERT_TRUE(r >= 0);
    return 0;
}

int test_tool_result_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_result");
    RUN_TEST(test_truncate_short);
    RUN_TEST(test_truncate_null);
    RUN_TEST(test_truncate_exact_limit);
    RUN_TEST(test_truncate_large);
    RUN_TEST(test_truncate_preserves_ends);
    RUN_TEST(test_msg_budget_under);
    RUN_TEST(test_msg_budget_over);
    RUN_TEST(test_msg_budget_null);
    RUN_TEST(test_cleanup_no_dir);
    RUN_TEST(test_cleanup_returns_count);
    TEST_RETURN_FAILED();
}
