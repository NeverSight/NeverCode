#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_init(void) {
    autofix_init();
    ASSERT_INT_EQ(autofix_count(), 0);
    autofix_free();
    return 0;
}
static int test_add(void) {
    autofix_init();
    int idx = autofix_add_issue("/tmp/test.c", 10, "unused variable 'x'");
    ASSERT(idx >= 0);
    ASSERT_INT_EQ(autofix_count(), 1);
    ASSERT_INT_EQ(autofix_pending_count(), 1);
    autofix_free();
    return 0;
}
static int test_categorize_import(void) {
    autofix_init();
    autofix_add_issue("test.c", 1, "missing import for stdio");
    autofix_free();
    return 0;
}
static int test_categorize_format(void) {
    autofix_init();
    autofix_add_issue("test.c", 1, "wrong indent level");
    autofix_free();
    return 0;
}
static int test_apply_no_cmd(void) {
    autofix_init();
    autofix_add_issue("/tmp/test.c", 5, "lint error");
    int r = autofix_apply(0);
    ASSERT(r != 0);
    autofix_free();
    return 0;
}
static int test_null_args(void) {
    autofix_init();
    ASSERT_INT_EQ(autofix_add_issue(NULL, 0, "msg"), -1);
    ASSERT_INT_EQ(autofix_add_issue("f", 0, NULL), -1);
    autofix_free();
    return 0;
}
static int test_category_names(void) {
    ASSERT_STR_EQ(fix_category_name(0), "lint");
    ASSERT_STR_EQ(fix_category_name(1), "format");
    return 0;
}
static int test_status_names(void) {
    ASSERT_STR_EQ(fix_status_name(0), "pending");
    ASSERT_STR_EQ(fix_status_name(1), "applied");
    return 0;
}
static int test_format(void) {
    autofix_init();
    autofix_add_issue("test.c", 10, "error here");
    char *s = autofix_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "test.c");
    free(s);
    autofix_free();
    return 0;
}

int test_autofix_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("autofix");
    RUN_TEST(test_init);
    RUN_TEST(test_add);
    RUN_TEST(test_categorize_import);
    RUN_TEST(test_categorize_format);
    RUN_TEST(test_apply_no_cmd);
    RUN_TEST(test_null_args);
    RUN_TEST(test_category_names);
    RUN_TEST(test_status_names);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
