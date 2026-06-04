#include "test.h"
#include "nevercode.h"
static int t1(void) {
    agent_mailbox_registry_clear();
    ASSERT_INT_EQ(agent_mailbox_registry_count(), 0);
    return 0;
}
static int t2(void) {
    agent_mailbox_registry_clear();
    agent_mailbox_t *m = agent_mailbox_registry_create("a1");
    ASSERT_NOT_NULL(m);
    ASSERT(agent_mailbox_registry_find("a1") == m);
    agent_mailbox_registry_clear();
    return 0;
}
static int t3(void) {
    agent_mailbox_registry_clear();
    agent_mailbox_t *m = agent_mailbox_registry_create("a2");
    uint64_t s = agent_mailbox_send(m, "s", "hello");
    ASSERT(s > 0);
    ASSERT_INT_EQ(agent_mailbox_peek(m), 1);
    char *f = NULL, *c = NULL;
    ASSERT_INT_EQ(agent_mailbox_recv(m, &f, &c), 0);
    ASSERT_STR_EQ(c, "hello");
    free(f);
    free(c);
    agent_mailbox_registry_clear();
    return 0;
}
static int t4(void) {
    agent_mailbox_registry_clear();
    agent_mailbox_t *m = agent_mailbox_registry_create("a3");
    ASSERT(agent_mailbox_recv(m, NULL, NULL) < 0);
    agent_mailbox_registry_clear();
    return 0;
}
static int t5(void) {
    agent_mailbox_registry_clear();
    agent_mailbox_t *m = agent_mailbox_registry_create("a4");
    uint64_t s1 = agent_mailbox_send(m, "a", "m1");
    uint64_t s2 = agent_mailbox_send(m, "a", "m2");
    ASSERT(s2 > s1);
    agent_mailbox_registry_clear();
    return 0;
}
static int t6(void) {
    agent_mailbox_registry_clear();
    agent_mailbox_t *m = agent_mailbox_registry_create("a5");
    agent_mailbox_send(m, "a", "m1");
    agent_mailbox_send(m, "b", "m2");
    agent_mailbox_send(m, "c", "m3");
    char *c[4];
    int n = agent_mailbox_drain(m, c, 4);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(c[0], "m1");
    for (int i = 0; i < n; i++)
        free(c[i]);
    agent_mailbox_registry_clear();
    return 0;
}
static int t7(void) {
    agent_mailbox_registry_clear();
    agent_mailbox_registry_create("x1");
    agent_mailbox_registry_create("x2");
    ASSERT_INT_EQ(agent_mailbox_registry_remove("x1"), 0);
    ASSERT_INT_EQ(agent_mailbox_registry_count(), 1);
    agent_mailbox_registry_clear();
    return 0;
}
static int t8(void) {
    agent_mailbox_registry_clear();
    agent_mailbox_t *m = agent_mailbox_registry_create("fmt");
    agent_mailbox_send(m, "s", "d");
    char *s = agent_mailbox_format_status(m);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "fmt");
    free(s);
    agent_mailbox_registry_clear();
    return 0;
}
static int t9(void) {
    ASSERT_INT_EQ(agent_mailbox_peek(NULL), 0);
    ASSERT(agent_mailbox_seq(NULL) == 0);
    ASSERT(agent_mailbox_send(NULL, "x", "y") == 0);
    return 0;
}
int test_mailbox_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("mailbox");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    TEST_RETURN_FAILED();
}
