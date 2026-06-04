#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_no_truncation_small(void) {
    char *result = tool_result_truncate("hello world", "Bash");
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "hello world");
    free(result);
    return 0;
}

static int test_no_truncation_null(void) {
    char *result = tool_result_truncate(NULL, "Bash");
    ASSERT_NULL(result);
    return 0;
}

static int test_no_truncation_exact_limit(void) {
    char *buf = malloc(TOOL_RESULT_MAX_CHARS + 1);
    memset(buf, 'x', TOOL_RESULT_MAX_CHARS);
    buf[TOOL_RESULT_MAX_CHARS] = '\0';
    char *result = tool_result_truncate(buf, "Bash");
    ASSERT_NOT_NULL(result);
    ASSERT(strlen(result) == TOOL_RESULT_MAX_CHARS);
    free(result);
    free(buf);
    return 0;
}

static int test_truncation_over_limit(void) {
    size_t big = TOOL_RESULT_MAX_CHARS + 1000;
    char *buf = malloc(big + 1);
    memset(buf, 'a', big);
    buf[big] = '\0';
    char *result = tool_result_truncate(buf, "Read");
    ASSERT_NOT_NULL(result);
    ASSERT(strlen(result) < big);
    ASSERT(strstr(result, "OUTPUT TRUNCATED") != NULL);
    ASSERT(strstr(result, "chars total") != NULL);
    free(result);
    free(buf);
    return 0;
}

static int test_truncation_preserves_head_tail(void) {
    size_t big = TOOL_RESULT_MAX_CHARS * 2;
    char *buf = malloc(big + 1);
    memset(buf, 'H', TOOL_RESULT_TRUNCATE_TO / 2);
    memset(buf + TOOL_RESULT_TRUNCATE_TO / 2, 'M', big - TOOL_RESULT_TRUNCATE_TO);
    memset(buf + big - TOOL_RESULT_TRUNCATE_TO / 2, 'T', TOOL_RESULT_TRUNCATE_TO / 2);
    buf[big] = '\0';

    char *result = tool_result_truncate(buf, "Grep");
    ASSERT_NOT_NULL(result);
    ASSERT(result[0] == 'H');
    ASSERT(result[strlen(result) - 1] == 'T');
    free(result);
    free(buf);
    return 0;
}

static int test_truncation_empty_string(void) {
    char *result = tool_result_truncate("", "Bash");
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "");
    free(result);
    return 0;
}

int test_tool_truncation_all(void) {
    TEST_SUITE("tool_truncation");
    TEST_COUNTERS;
    RUN_TEST(test_no_truncation_small);
    RUN_TEST(test_no_truncation_null);
    RUN_TEST(test_no_truncation_exact_limit);
    RUN_TEST(test_truncation_over_limit);
    RUN_TEST(test_truncation_preserves_head_tail);
    RUN_TEST(test_truncation_empty_string);
    TEST_RETURN_FAILED();
}
