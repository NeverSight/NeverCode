/*
 * test_compact_template.c — Tests for compact template system.
 */
#include "test.h"
#include "nevercode.h"

static int test_init_empty(void) {
    compact_tpl_init();
    ASSERT_INT_EQ(0, compact_tpl_count());
    compact_tpl_free();
    return 0;
}

static int test_add_and_get(void) {
    compact_tpl_init();
    ASSERT_INT_EQ(0, compact_tpl_add("test", "body %d %d %s"));
    ASSERT_INT_EQ(1, compact_tpl_count());
    ASSERT_STR_EQ("body %d %d %s", compact_tpl_get("test"));
    compact_tpl_free();
    return 0;
}

static int test_default_template(void) {
    compact_tpl_init();
    const char *d = compact_tpl_default();
    ASSERT_NOT_NULL(d);
    ASSERT_STR_CONTAINS(d, "CONTEXT CHECKPOINT");
    ASSERT_STR_CONTAINS(d, "handoff");
    char *r = compact_tpl_render(NULL, 10, 5000, "sess1");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "10");
    free(r);
    compact_tpl_free();
    return 0;
}

static int test_summary_prefix(void) {
    compact_tpl_init();
    const char *sp = compact_tpl_summary_prefix();
    ASSERT_NOT_NULL(sp);
    ASSERT_STR_CONTAINS(sp, "agent");
    ASSERT_STR_CONTAINS(sp, "summary");
    compact_tpl_free();
    return 0;
}

static int test_mid_turn_prefix(void) {
    compact_tpl_init();
    const char *mp = compact_tpl_mid_turn_prefix();
    ASSERT_NOT_NULL(mp);
    ASSERT_STR_CONTAINS(mp, "compacted");
    ASSERT_STR_CONTAINS(mp, "in progress");
    compact_tpl_free();
    return 0;
}

static int test_mid_turn_custom_override(void) {
    compact_tpl_init();
    ASSERT_INT_EQ(0, compact_tpl_add("mid_turn_prefix", "Custom mid: pause here.\n"));
    ASSERT_STR_EQ(compact_tpl_mid_turn_prefix(), "Custom mid: pause here.\n");
    compact_tpl_free();
    return 0;
}

static int test_format_list(void) {
    compact_tpl_init();
    char *s = compact_tpl_format_list();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Templates");
    free(s);
    compact_tpl_free();
    return 0;
}

static int test_custom_overrides_default(void) {
    compact_tpl_init();
    ASSERT_INT_EQ(0, compact_tpl_add("default", "Custom: %d msgs, %d tokens, %s"));
    const char *d = compact_tpl_default();
    ASSERT_STR_CONTAINS(d, "Custom");
    compact_tpl_free();
    return 0;
}

static int test_summary_budget_scaling(void) {
    int small = compact_tpl_summary_budget(1000);
    ASSERT_TRUE(small >= COMPACT_SUMMARY_MIN_TOKENS);

    int proportional = compact_tpl_summary_budget(50000);
    ASSERT_TRUE(proportional >= COMPACT_SUMMARY_MIN_TOKENS);
    ASSERT_TRUE(proportional <= COMPACT_SUMMARY_MAX_TOKENS);
    ASSERT_TRUE(proportional == (int)(50000 * COMPACT_SUMMARY_RATIO));

    int capped = compact_tpl_summary_budget(500000);
    ASSERT_INT_EQ(COMPACT_SUMMARY_MAX_TOKENS, capped);
    return 0;
}

static int test_prior_summary_detection(void) {
    ASSERT_TRUE(!compact_tpl_has_prior_summary(NULL));
    ASSERT_TRUE(!compact_tpl_has_prior_summary("Hello world"));
    ASSERT_TRUE(
        compact_tpl_has_prior_summary("[CONTEXT COMPACTION] Earlier turns were compacted."));
    ASSERT_TRUE(compact_tpl_has_prior_summary("some text before [CONTEXT COMPACTION] and after"));
    return 0;
}

static int test_iterative_prompt(void) {
    char *p = compact_tpl_build_iterative_prompt(
        "## Goal\nBuild feature X\n## Progress\n- Step 1 done", 20, 8000, "sess-iter");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "updating an existing context summary");
    ASSERT_STR_CONTAINS(p, "Build feature X");
    ASSERT_STR_CONTAINS(p, "Step 1 done");
    ASSERT_STR_CONTAINS(p, "20 messages");
    ASSERT_STR_CONTAINS(p, "sess-iter");
    free(p);
    return 0;
}

static int test_template_has_critical_context(void) {
    compact_tpl_init();
    const char *d = compact_tpl_default();
    ASSERT_NOT_NULL(d);
    ASSERT_STR_CONTAINS(d, "Critical Context");
    ASSERT_STR_CONTAINS(d, "Constraints & Preferences");
    ASSERT_STR_CONTAINS(d, "same language");
    ASSERT_STR_CONTAINS(d, "### Done");
    ASSERT_STR_CONTAINS(d, "### In Progress");
    ASSERT_STR_CONTAINS(d, "### Blocked");
    compact_tpl_free();
    return 0;
}

static int test_iterative_has_critical_context(void) {
    char *p = compact_tpl_build_iterative_prompt("prior", 5, 2000, "s1");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "Critical Context");
    ASSERT_STR_CONTAINS(p, "same language");
    free(p);
    return 0;
}

static int test_prune_threshold_macros(void) {
    ASSERT_TRUE(COMPACT_PRUNE_MIN_TOKENS > 0);
    ASSERT_TRUE(COMPACT_PRUNE_PROTECT_TOKENS > COMPACT_PRUNE_MIN_TOKENS);
    ASSERT_TRUE(COMPACT_SERIALIZE_TRUNC_CHARS > 0);
    ASSERT_TRUE(COMPACT_SERIALIZE_HEAD_CHARS < COMPACT_SERIALIZE_TRUNC_CHARS);
    ASSERT_TRUE(COMPACT_SERIALIZE_TAIL_CHARS < COMPACT_SERIALIZE_TRUNC_CHARS);
    ASSERT_TRUE(COMPACT_SERIALIZE_ARGS_MAX > 0);
    return 0;
}

int test_compact_template_all(void) {
    TEST_SUITE("compact_template");
    TEST_COUNTERS;
    RUN_TEST(test_init_empty);
    RUN_TEST(test_add_and_get);
    RUN_TEST(test_default_template);
    RUN_TEST(test_summary_prefix);
    RUN_TEST(test_mid_turn_prefix);
    RUN_TEST(test_mid_turn_custom_override);
    RUN_TEST(test_format_list);
    RUN_TEST(test_custom_overrides_default);
    RUN_TEST(test_summary_budget_scaling);
    RUN_TEST(test_prior_summary_detection);
    RUN_TEST(test_iterative_prompt);
    RUN_TEST(test_template_has_critical_context);
    RUN_TEST(test_iterative_has_critical_context);
    RUN_TEST(test_prune_threshold_macros);
    TEST_RETURN_FAILED();
}
