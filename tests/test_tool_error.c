#include "test.h"
#include "nevercode.h"

typedef struct tool_error_t tool_error_t;

void terr_init(tool_error_t *err);
void terr_set(tool_error_t *err, int kind, const char *tool, const char *message);
void terr_set_detail(tool_error_t *err, const char *fmt, ...);
const char *terr_kind_name(int kind);
char *terr_format(const tool_error_t *err);
char *terr_to_json(const tool_error_t *err);
int terr_from_exit_code(int exit_code);
int terr_is_retryable(const tool_error_t *err);
int terr_has_error(const tool_error_t *err);

static char err_buf[4096];

static int test_init(void) {
    tool_error_t *e = (tool_error_t *)err_buf;
    terr_init(e);
    ASSERT_FALSE(terr_has_error(e));
    ASSERT_FALSE(terr_is_retryable(e));
    return 0;
}

static int test_set_error(void) {
    tool_error_t *e = (tool_error_t *)err_buf;
    terr_init(e);
    terr_set(e, 4 /* TERR_TIMEOUT */, "Bash", "command timed out");
    ASSERT_TRUE(terr_has_error(e));
    ASSERT_TRUE(terr_is_retryable(e));

    char *fmt = terr_format(e);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "Bash");
    ASSERT_STR_CONTAINS(fmt, "timed out");
    ASSERT_STR_CONTAINS(fmt, "retryable");
    free(fmt);
    return 0;
}

static int test_kind_names(void) {
    ASSERT_STR_EQ(terr_kind_name(0), "none");
    ASSERT_STR_EQ(terr_kind_name(1), "invalid_input");
    ASSERT_STR_EQ(terr_kind_name(4), "timeout");
    ASSERT_STR_EQ(terr_kind_name(99), "unknown");
    return 0;
}

static int test_from_exit_code(void) {
    ASSERT_INT_EQ(terr_from_exit_code(0), 0);   /* TERR_NONE */
    ASSERT_INT_EQ(terr_from_exit_code(124), 4); /* TERR_TIMEOUT */
    ASSERT_INT_EQ(terr_from_exit_code(137), 4); /* TERR_TIMEOUT */
    ASSERT_INT_EQ(terr_from_exit_code(126), 3); /* TERR_PERMISSION */
    ASSERT_INT_EQ(terr_from_exit_code(127), 2); /* TERR_NOT_FOUND */
    ASSERT_INT_EQ(terr_from_exit_code(1), 5);   /* TERR_EXEC_FAILED */
    return 0;
}

static int test_to_json(void) {
    tool_error_t *e = (tool_error_t *)err_buf;
    terr_init(e);
    terr_set(e, 2 /* TERR_NOT_FOUND */, "Read", "file not found");
    terr_set_detail(e, "path: /foo/bar.c");

    char *json = terr_to_json(e);
    ASSERT_NOT_NULL(json);
    ASSERT_STR_CONTAINS(json, "not_found");
    ASSERT_STR_CONTAINS(json, "Read");
    ASSERT_STR_CONTAINS(json, "/foo/bar.c");
    free(json);
    return 0;
}

static int test_non_retryable(void) {
    tool_error_t *e = (tool_error_t *)err_buf;
    terr_init(e);
    terr_set(e, 1 /* TERR_INVALID_INPUT */, "Grep", "bad regex");
    ASSERT_TRUE(terr_has_error(e));
    ASSERT_FALSE(terr_is_retryable(e));
    return 0;
}

static int test_format_none(void) {
    tool_error_t *e = (tool_error_t *)err_buf;
    terr_init(e);
    ASSERT_NULL(terr_format(e));
    ASSERT_NULL(terr_to_json(e));
    return 0;
}

int test_tool_error_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_error");
    RUN_TEST(test_init);
    RUN_TEST(test_set_error);
    RUN_TEST(test_kind_names);
    RUN_TEST(test_from_exit_code);
    RUN_TEST(test_to_json);
    RUN_TEST(test_non_retryable);
    RUN_TEST(test_format_none);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_error");
    RUN_TEST(test_init);
    RUN_TEST(test_set_error);
    RUN_TEST(test_kind_names);
    RUN_TEST(test_from_exit_code);
    RUN_TEST(test_to_json);
    RUN_TEST(test_non_retryable);
    RUN_TEST(test_format_none);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
