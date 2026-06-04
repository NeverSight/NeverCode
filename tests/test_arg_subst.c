/*
 * test_arg_subst.c — Tests for $ARGUMENTS placeholder substitution.
 */
#include "test.h"
#include "nevercode.h"

static int t_null(void) {
    ASSERT_NULL(arg_subst_expand(NULL, "args", NULL, 0));
    return 0;
}

static int t_no_placeholders(void) {
    char *r = arg_subst_expand("hello world", "foo", NULL, 0);
    ASSERT_STR_EQ(r, "hello world");
    free(r);
    return 0;
}

static int t_full_args(void) {
    char *r = arg_subst_expand("do $ARGUMENTS now", "fix the bug", NULL, 0);
    ASSERT_STR_EQ(r, "do fix the bug now");
    free(r);
    return 0;
}

static int t_indexed(void) {
    char *r = arg_subst_expand("file=$0 line=$1", "main.c 42", NULL, 0);
    ASSERT_STR_EQ(r, "file=main.c line=42");
    free(r);
    return 0;
}

static int t_brackets(void) {
    char *r = arg_subst_expand("$ARGUMENTS[0] and $ARGUMENTS[1]", "hello world", NULL, 0);
    ASSERT_STR_EQ(r, "hello and world");
    free(r);
    return 0;
}

static int t_named(void) {
    const char *names[] = {"file", "line"};
    char *r = arg_subst_expand("Edit $file at $line", "main.c 42", names, 2);
    ASSERT_STR_EQ(r, "Edit main.c at 42");
    free(r);
    return 0;
}

static int t_empty_args(void) {
    char *r = arg_subst_expand("do $ARGUMENTS end", "", NULL, 0);
    ASSERT_STR_EQ(r, "do  end");
    free(r);
    return 0;
}

static int t_macros(void) {
    ASSERT(ARG_SUBST_MAX_ARGS > 0);
    return 0;
}

int test_arg_subst_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("arg_subst");
    RUN_TEST(t_null);
    RUN_TEST(t_no_placeholders);
    RUN_TEST(t_full_args);
    RUN_TEST(t_indexed);
    RUN_TEST(t_brackets);
    RUN_TEST(t_named);
    RUN_TEST(t_empty_args);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
