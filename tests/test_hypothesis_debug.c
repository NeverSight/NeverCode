#include "test.h"
#include "nevercode.h"
#include "harness.h"
static int t1(void) {
    hypothesis_debug_init();
    ASSERT_INT_EQ(hypothesis_debug_session_count(), 0);
    hypothesis_debug_free();
    return 0;
}
static int t2(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("err");
    ASSERT_INT_EQ(s, 0);
    ASSERT_INT_EQ(hypothesis_debug_session_count(), 1);
    hypothesis_debug_free();
    return 0;
}
static int t3(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("seg");
    int h = hypothesis_debug_propose(s, "null ptr", 0.7);
    ASSERT_INT_EQ(h, 0);
    ASSERT_INT_EQ(hypothesis_debug_hyp_count(s), 1);
    hypothesis_debug_free();
    return 0;
}
static int t4(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("f");
    int h = hypothesis_debug_propose(s, "h", 0.6);
    hypothesis_debug_test(s, h);
    hypothesis_debug_confirm(s, h, "proof");
    ASSERT_INT_EQ(hypothesis_debug_confirmed_count(s), 1);
    hypothesis_debug_free();
    return 0;
}
static int t5(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("c");
    int h = hypothesis_debug_propose(s, "x", 0.5);
    hypothesis_debug_test(s, h);
    hypothesis_debug_refute(s, h, "nope");
    ASSERT_INT_EQ(hypothesis_debug_refuted_count(s), 1);
    hypothesis_debug_free();
    return 0;
}
static int t6(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("API 500");
    hypothesis_debug_propose(s, "overload", 0.8);
    char *p = hypothesis_debug_build_prompt(s);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "debugging_context");
    ASSERT_STR_CONTAINS(p, "API 500");
    free(p);
    hypothesis_debug_free();
    return 0;
}
static int t7(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("sym");
    hypothesis_debug_propose(s, "h1", 0.5);
    char *f = hypothesis_debug_format(s);
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Debug Session");
    free(f);
    hypothesis_debug_free();
    return 0;
}
static int t8(void) {
    hypothesis_debug_init();
    hypothesis_debug_begin("s1");
    hypothesis_debug_begin("s2");
    char *f = hypothesis_debug_format_all();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "2 sessions");
    free(f);
    hypothesis_debug_free();
    return 0;
}
static int t9(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("x");
    hypothesis_debug_propose(s, "h", 0.5);
    ASSERT_INT_EQ(hypothesis_debug_update_confidence(s, 0, 0.9), 0);
    hypothesis_debug_free();
    return 0;
}
static int t10(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("x");
    ASSERT_INT_EQ(hypothesis_debug_end(s), 0);
    hypothesis_debug_free();
    return 0;
}
static int t11(void) {
    hypothesis_debug_init();
    ASSERT_INT_EQ(hypothesis_debug_propose(-1, "h", 0.5), -1);
    ASSERT_INT_EQ(hypothesis_debug_begin(NULL), -1);
    hypothesis_debug_free();
    return 0;
}
static int t_auto_loop(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("loop stall");
    ASSERT(s >= 0);
    int added = hypothesis_debug_auto_populate(s, 1);
    ASSERT(added > 0);
    ASSERT(hypothesis_debug_hyp_count(s) == added);
    char *prompt = hypothesis_debug_build_prompt(s);
    ASSERT(prompt != NULL);
    ASSERT(strstr(prompt, "Tool output empty") != NULL || strstr(prompt, "proposed") != NULL);
    free(prompt);
    hypothesis_debug_free();
    return 0;
}
static int t_auto_noprog(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("no progress");
    int added = hypothesis_debug_auto_populate(s, 2);
    ASSERT(added > 0);
    char *prompt = hypothesis_debug_build_prompt(s);
    ASSERT(strstr(prompt, "Hypotheses") != NULL);
    ASSERT(strstr(prompt, "(0 total") == NULL);
    free(prompt);
    hypothesis_debug_free();
    return 0;
}
static int t_auto_timeout(void) {
    hypothesis_debug_init();
    int s = hypothesis_debug_begin("timeout");
    int added = hypothesis_debug_auto_populate(s, 3);
    ASSERT(added > 0);
    hypothesis_debug_free();
    return 0;
}
static int t_auto_invalid(void) {
    hypothesis_debug_init();
    ASSERT_INT_EQ(hypothesis_debug_auto_populate(-1, 1), -1);
    ASSERT_INT_EQ(hypothesis_debug_auto_populate(99, 1), -1);
    hypothesis_debug_free();
    return 0;
}
int test_hypothesis_debug_all(void) {
    TEST_SUITE("hypothesis_debug");
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
    RUN_TEST(t10);
    RUN_TEST(t11);
    RUN_TEST(t_auto_loop);
    RUN_TEST(t_auto_noprog);
    RUN_TEST(t_auto_timeout);
    RUN_TEST(t_auto_invalid);
    TEST_RETURN_FAILED();
}
