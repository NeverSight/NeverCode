#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include <stdlib.h>
#include <string.h>

static int test_read_concurrency_safe(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("Read");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == true);
    return 0;
}

static int test_bash_not_concurrency_safe(void) {
    const tool_def_t *t = tools_find("Bash");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == false);
    return 0;
}

static int test_glob_concurrency_safe(void) {
    const tool_def_t *t = tools_find("Glob");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == true);
    return 0;
}

static int test_grep_concurrency_safe(void) {
    const tool_def_t *t = tools_find("Grep");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == true);
    return 0;
}

static int test_web_fetch_concurrency_safe(void) {
    const tool_def_t *t = tools_find("WebFetch");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == true);
    return 0;
}

static int test_write_not_concurrency_safe(void) {
    const tool_def_t *t = tools_find("Write");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == false);
    return 0;
}

static int test_edit_not_concurrency_safe(void) {
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == false);
    return 0;
}

static int test_notebook_not_concurrency_safe(void) {
    const tool_def_t *t = tools_find("NotebookEdit");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == false);
    return 0;
}

int test_parallel_tools_all(void) {
    TEST_SUITE("parallel_tools");
    TEST_COUNTERS;
    RUN_TEST(test_read_concurrency_safe);
    RUN_TEST(test_bash_not_concurrency_safe);
    RUN_TEST(test_glob_concurrency_safe);
    RUN_TEST(test_grep_concurrency_safe);
    RUN_TEST(test_web_fetch_concurrency_safe);
    RUN_TEST(test_write_not_concurrency_safe);
    RUN_TEST(test_edit_not_concurrency_safe);
    RUN_TEST(test_notebook_not_concurrency_safe);
    TEST_RETURN_FAILED();
}
