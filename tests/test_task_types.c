#include "test.h"
#include "nevercode.h"
static int t1(void) {
    task_types_init();
    ASSERT_INT_EQ(0, task_types_count());
    task_types_free();
    return 0;
}
static int t2(void) {
    task_types_init();
    int id = task_types_create(TASK_KIND_REGULAR, "t", "s1");
    ASSERT_GE(id, 0);
    task_types_start(id);
    ASSERT_TRUE(task_types_running(id));
    task_types_finish(id, 0, "ok");
    ASSERT_TRUE(task_types_done(id));
    task_types_free();
    return 0;
}
static int t3(void) {
    task_types_init();
    int id = task_types_create(TASK_KIND_COMPACT, "c", "s2");
    task_types_start(id);
    task_types_abort(id);
    ASSERT_TRUE(task_types_done(id));
    task_types_free();
    return 0;
}
static int t4(void) {
    ASSERT_STR_EQ("regular", task_kind_name(TASK_KIND_REGULAR));
    ASSERT_STR_EQ("compact", task_kind_name(TASK_KIND_COMPACT));
    return 0;
}
static int t5(void) {
    task_types_init();
    task_types_create(TASK_KIND_REGULAR, "a", "s1");
    task_types_create(TASK_KIND_COMPACT, "b", "s1");
    task_types_create(TASK_KIND_REGULAR, "c", "s2");
    int ids[4];
    int n = task_types_by_kind(TASK_KIND_REGULAR, ids, 4);
    ASSERT_INT_EQ(2, n);
    char *s = task_types_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Tasks");
    free(s);
    task_types_free();
    return 0;
}
int test_task_types_all(void) {
    TEST_SUITE("task_types");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    TEST_RETURN_FAILED();
}
