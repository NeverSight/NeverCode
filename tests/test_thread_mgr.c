#include "test.h"
#include "nevercode.h"
static int t1(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    ASSERT_INT_EQ(t.count, 0);
    thread_mgr_free(&t);
    return 0;
}
static int t2(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    int idx = thread_mgr_create(&t, "p", NULL, "gpt-5.4");
    ASSERT_INT_EQ(idx, 0);
    ASSERT_STR_EQ(t.threads[0].label, "p");
    ASSERT_NOT_NULL(t.threads[0].id);
    thread_mgr_free(&t);
    return 0;
}
static int t3(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    thread_mgr_create(&t, "a", NULL, NULL);
    thread_mgr_create(&t, "b", NULL, NULL);
    ASSERT_TRUE(strcmp(t.threads[0].id, t.threads[1].id) != 0);
    ASSERT_INT_EQ(thread_mgr_find(&t, t.threads[1].id), 1);
    thread_mgr_free(&t);
    return 0;
}
static int t4(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    int idx = thread_mgr_create(&t, "s", NULL, NULL);
    ASSERT_INT_EQ(thread_mgr_suspend(&t, idx), 0);
    ASSERT_INT_EQ(t.threads[idx].state, THREAD_SUSPENDED);
    ASSERT_INT_EQ(thread_mgr_resume(&t, idx), 0);
    ASSERT_INT_EQ(t.threads[idx].state, THREAD_ACTIVE);
    thread_mgr_free(&t);
    return 0;
}
static int t5(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    int idx = thread_mgr_create(&t, "c", NULL, NULL);
    thread_mgr_complete(&t, idx);
    ASSERT_INT_EQ(t.threads[idx].state, THREAD_COMPLETED);
    thread_mgr_free(&t);
    return 0;
}
static int t6(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    int idx = thread_mgr_create(&t, "a", NULL, NULL);
    thread_mgr_abort(&t, idx);
    ASSERT_INT_EQ(t.threads[idx].state, THREAD_ABORTED);
    thread_mgr_free(&t);
    return 0;
}
static int t7(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    int idx = thread_mgr_create(&t, "r", NULL, NULL);
    thread_mgr_record_turn(&t, idx);
    thread_mgr_record_turn(&t, idx);
    ASSERT_INT_EQ(t.threads[idx].turn_count, 2);
    thread_mgr_free(&t);
    return 0;
}
static int t8(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    thread_mgr_create(&t, "1", NULL, NULL);
    thread_mgr_create(&t, "2", NULL, NULL);
    thread_mgr_create(&t, "3", NULL, NULL);
    ASSERT_INT_EQ(thread_mgr_active_count(&t), 3);
    thread_mgr_suspend(&t, 1);
    ASSERT_INT_EQ(thread_mgr_active_count(&t), 2);
    thread_mgr_free(&t);
    return 0;
}
static int t9(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    thread_mgr_create(&t, "p", NULL, NULL);
    const char *pid = t.threads[0].id;
    thread_mgr_create(&t, "c1", pid, NULL);
    thread_mgr_create(&t, "c2", pid, NULL);
    int ch[8];
    ASSERT_INT_EQ(thread_mgr_children(&t, pid, ch, 8), 2);
    thread_mgr_free(&t);
    return 0;
}
static int ta(void) {
    ASSERT_STR_EQ(thread_state_name(THREAD_ACTIVE), "active");
    ASSERT_STR_EQ(thread_state_name(THREAD_COMPLETED), "completed");
    return 0;
}
static int tb(void) {
    thread_mgr_t t;
    thread_mgr_init(&t);
    char *s = thread_mgr_format(&t);
    ASSERT_STR_CONTAINS(s, "No threads");
    free(s);
    thread_mgr_create(&t, "lb", NULL, NULL);
    s = thread_mgr_format(&t);
    ASSERT_STR_CONTAINS(s, "lb");
    free(s);
    thread_mgr_free(&t);
    return 0;
}
static int tc(void) {
    ASSERT_NOT_NULL(thread_mgr_global());
    return 0;
}
static int td(void) {
    ASSERT_INT_EQ(thread_mgr_create(NULL, "x", NULL, NULL), -1);
    ASSERT_INT_EQ(thread_mgr_find(NULL, "x"), -1);
    ASSERT_INT_EQ(thread_mgr_active_count(NULL), 0);
    return 0;
}
int test_thread_mgr_all(void) {
    TEST_SUITE("thread_mgr");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(ta);
    RUN_TEST(tb);
    RUN_TEST(tc);
    RUN_TEST(td);
    TEST_RETURN_FAILED();
}
