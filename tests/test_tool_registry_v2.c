#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include <string.h>

static int dummy_exec(const cJSON *input, char **output, void *ctx) {
    (void)input;
    (void)ctx;
    *output = nc_xstrdup("ok");
    return 0;
}

static int test_register_dedup(void) {
    int before = tools_count();

    tool_def_t t1 = {0};
    t1.name = "__test_dedup_tool__";
    t1.description = "v1";
    t1.execute = dummy_exec;
    tools_register(&t1);
    int after1 = tools_count();
    ASSERT_INT_EQ(after1, before + 1);

    tool_def_t t2 = {0};
    t2.name = "__test_dedup_tool__";
    t2.description = "v2";
    t2.execute = dummy_exec;
    tools_register(&t2);
    int after2 = tools_count();
    ASSERT_INT_EQ(after2, after1);

    const tool_def_t *found = tools_find("__test_dedup_tool__");
    ASSERT_NOT_NULL(found);
    ASSERT_STR_EQ(found->description, "v2");
    return 0;
}

static int test_git_diff_registered(void) {
    const tool_def_t *t = tools_find("GitDiff");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);
    return 0;
}

static int test_find_returns_null_for_unknown(void) {
    const tool_def_t *t = tools_find("__absolutely_not_a_tool__");
    ASSERT_NULL(t);
    return 0;
}

int test_tool_registry_v2_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_registry_v2");
    RUN_TEST(test_register_dedup);
    RUN_TEST(test_git_diff_registered);
    RUN_TEST(test_find_returns_null_for_unknown);
    TEST_RETURN_FAILED();
}
