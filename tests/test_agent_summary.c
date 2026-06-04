#include "test.h"
#include "nevercode.h"
static int t1(void) {
    agent_summary_init();
    ASSERT_INT_EQ(agent_summary_count(), 0);
    agent_summary_free();
    return 0;
}
static int t2(void) {
    agent_summary_init();
    ASSERT_INT_EQ(agent_summary_update("a1", "Reading main.c"), 0);
    ASSERT_STR_EQ(agent_summary_get("a1"), "Reading main.c");
    agent_summary_free();
    return 0;
}
static int t3(void) {
    agent_summary_init();
    agent_summary_update("a1", "s1");
    agent_summary_update("a1", "s2");
    ASSERT_INT_EQ(agent_summary_count(), 1);
    ASSERT_STR_EQ(agent_summary_get("a1"), "s2");
    agent_summary_free();
    return 0;
}
static int t4(void) {
    agent_summary_init();
    ASSERT_NULL(agent_summary_get("x"));
    ASSERT_TRUE(agent_summary_stale("x", 1));
    agent_summary_free();
    return 0;
}
static int t5(void) {
    agent_summary_init();
    agent_summary_update("a1", "w");
    ASSERT_INT_EQ(agent_summary_remove("a1"), 0);
    ASSERT_INT_EQ(agent_summary_count(), 0);
    agent_summary_free();
    return 0;
}
static int t6(void) {
    char *s = agent_summary_extract_from_output("l1\nl2\nlast");
    ASSERT_STR_EQ(s, "last");
    free(s);
    return 0;
}
static int t7(void) {
    char *s = agent_summary_extract_from_output("");
    ASSERT_NOT_NULL(s);
    free(s);
    s = agent_summary_extract_from_output(NULL);
    ASSERT_NOT_NULL(s);
    free(s);
    return 0;
}
static int t8(void) {
    agent_summary_init();
    agent_summary_update("x1", "Running tests");
    char *f = agent_summary_format_all();
    ASSERT_STR_CONTAINS(f, "x1");
    ASSERT_STR_CONTAINS(f, "Running tests");
    free(f);
    agent_summary_free();
    return 0;
}
static int t9(void) {
    ASSERT(agent_summary_update(NULL, "x") < 0);
    ASSERT(agent_summary_update("x", NULL) < 0);
    ASSERT(agent_summary_remove("nonexistent") < 0);
    return 0;
}
int test_agent_summary_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_summary");
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
