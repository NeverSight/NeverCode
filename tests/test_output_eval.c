#include "test.h"
#include "harness.h"
#include "defaults.h"
static int t_oe_init(void) {
    output_eval_init();
    ASSERT_INT_EQ(output_eval_total(), 0);
    output_eval_free();
    return 0;
}
static int t_oe_pass(void) {
    output_eval_init();
    oeval_result_t r = output_eval_check_ex(
        "Here is code:\n```c\nint main(){return 0;}\n```\nDone.", "write a main", 2);
    ASSERT_INT_EQ(r.verdict, 0);
    ASSERT_TRUE(r.confidence >= OEVAL_PASS_THRESHOLD);
    output_eval_free();
    return 0;
}
static int t_oe_empty(void) {
    output_eval_init();
    oeval_result_t r = output_eval_check_ex("", "do something", 0);
    ASSERT_INT_EQ(r.verdict, 4);
    ASSERT_TRUE(output_eval_needs_retry(r.verdict));
    output_eval_free();
    return 0;
}
static int t_oe_null(void) {
    output_eval_init();
    oeval_result_t r = output_eval_check_ex(NULL, "g", 0);
    ASSERT_INT_EQ(r.verdict, 4);
    output_eval_free();
    return 0;
}
static int t_oe_vname(void) {
    ASSERT_STR_EQ(output_eval_verdict_name(0), "pass");
    ASSERT_STR_EQ(output_eval_verdict_name(4), "empty");
    ASSERT_STR_EQ(output_eval_verdict_name(99), "unknown");
    return 0;
}
static int t_oe_retry(void) {
    char *p = output_eval_retry_prompt(4, "t");
    ASSERT_NOT_NULL(p);
    free(p);
    p = output_eval_retry_prompt(0, "t");
    ASSERT_NULL(p);
    return 0;
}
static int t_oe_degrade(void) {
    int i;
    output_eval_init();
    for (i = 0; i < 4; i++)
        output_eval_check_ex("", "g", 0);
    ASSERT_TRUE(output_eval_degrading());
    output_eval_free();
    return 0;
}
static int t_oe_fmt(void) {
    output_eval_init();
    output_eval_check_ex("answer", "g", 1);
    char *s = output_eval_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Evaluator:");
    free(s);
    output_eval_free();
    return 0;
}
static int t_oe_short_expected(void) {
    output_eval_init();
    oeval_result_t r = output_eval_check_ex("YES", "Reply with only the word YES", 0);
    ASSERT_TRUE(r.confidence >= OEVAL_PASS_THRESHOLD);
    ASSERT_INT_EQ(r.verdict, 0);
    ASSERT_FALSE(output_eval_needs_retry(r.verdict));
    output_eval_free();
    return 0;
}
static int t_oe_concise_pass(void) {
    output_eval_init();
    oeval_result_t r = output_eval_check_ex("42", "briefly, what is 6*7?", 0);
    ASSERT_TRUE(r.confidence >= OEVAL_PASS_THRESHOLD);
    output_eval_free();
    return 0;
}

static int t_oe_snapshot_restore(void) {
    output_eval_init();
    output_eval_check_ex("Done.\n```c\nint main(void){return 0;}\n```", "write a main", 1);
    for (int i = 0; i < OEVAL_DEGRADE_THRESHOLD; i++)
        output_eval_check_ex("", "g", 0);

    output_eval_snapshot_t snap;
    output_eval_snapshot(&snap);
    ASSERT_INT_EQ(snap.total_evals, OEVAL_DEGRADE_THRESHOLD + 1);
    ASSERT_INT_EQ(snap.pass_count, 1);
    ASSERT_INT_EQ(snap.empty_count, OEVAL_DEGRADE_THRESHOLD);

    output_eval_free();
    output_eval_restore(&snap);
    ASSERT_INT_EQ(output_eval_total(), OEVAL_DEGRADE_THRESHOLD + 1);
    ASSERT_INT_EQ(output_eval_pass_count(), 1);
    ASSERT_INT_EQ(output_eval_consecutive_weak(), OEVAL_DEGRADE_THRESHOLD);
    ASSERT_TRUE(output_eval_degrading());

    char *s = output_eval_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "consecutive_weak");
    free(s);

    output_eval_free();
    return 0;
}

int test_output_eval_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("output_eval");
    RUN_TEST(t_oe_init);
    RUN_TEST(t_oe_pass);
    RUN_TEST(t_oe_empty);
    RUN_TEST(t_oe_null);
    RUN_TEST(t_oe_vname);
    RUN_TEST(t_oe_retry);
    RUN_TEST(t_oe_degrade);
    RUN_TEST(t_oe_fmt);
    RUN_TEST(t_oe_short_expected);
    RUN_TEST(t_oe_concise_pass);
    RUN_TEST(t_oe_snapshot_restore);
    TEST_RETURN_FAILED();
}
