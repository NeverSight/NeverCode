#include "test.h"
#include "nevercode.h"
#include "tools.h"

static int t_discover(void) {
    workflow_discover();
    ASSERT_TRUE(workflow_count() >= 0);
    return 0;
}

static int t_find_missing(void) {
    workflow_discover();
    ASSERT_INT_EQ(workflow_find("nonexistent_wf_xyz"), -1);
    return 0;
}

static int t_list_format(void) {
    workflow_discover();
    char *s = workflow_list_format();
    ASSERT_NOT_NULL(s);
    ASSERT_TRUE(strlen(s) > 0);
    free(s);
    return 0;
}

static int t_null_safety(void) {
    ASSERT_NULL(workflow_name(-1));
    ASSERT_NULL(workflow_name(999));
    ASSERT_NULL(workflow_desc(-1));
    return 0;
}

int test_workflow_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("workflow");
    RUN_TEST(t_discover);
    RUN_TEST(t_find_missing);
    RUN_TEST(t_list_format);
    RUN_TEST(t_null_safety);
    TEST_RETURN_FAILED();
}
