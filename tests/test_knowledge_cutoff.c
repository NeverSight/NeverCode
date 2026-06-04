#include "test.h"
#include "nevercode.h"

static int t_gpt54(void) {
    const char *c = kc_lookup("gpt-5.4");
    ASSERT_NOT_NULL(c);
    ASSERT_STR_EQ(c, KC_GPT54_CUTOFF);
    return 0;
}

static int t_gpt54_mini(void) {
    const char *c = kc_lookup("gpt-5.4-mini");
    ASSERT_NOT_NULL(c);
    ASSERT_STR_EQ(c, KC_GPT54_CUTOFF);
    return 0;
}

static int t_opus46(void) {
    const char *c = kc_lookup("claude-opus-4-6-20260101");
    ASSERT_NOT_NULL(c);
    ASSERT_STR_EQ(c, KC_OPUS46_CUTOFF);
    return 0;
}

static int t_sonnet46(void) {
    const char *c = kc_lookup("claude-sonnet-4-6");
    ASSERT_NOT_NULL(c);
    ASSERT_STR_EQ(c, KC_SONNET46_CUTOFF);
    return 0;
}

static int t_deepseek_r1(void) {
    const char *c = kc_lookup("deepseek-r1");
    ASSERT_NOT_NULL(c);
    ASSERT_STR_EQ(c, KC_DSR1_CUTOFF);
    return 0;
}

static int t_qwen3(void) {
    const char *c = kc_lookup("qwen3-coder-235b");
    ASSERT_NOT_NULL(c);
    ASSERT_STR_EQ(c, KC_QWEN3_CUTOFF);
    return 0;
}

static int t_unknown(void) {
    ASSERT_NULL(kc_lookup("totally-unknown-model"));
    return 0;
}

static int t_null_safety(void) {
    ASSERT_NULL(kc_lookup(NULL));
    ASSERT_NULL(kc_lookup(""));
    return 0;
}

static int t_build_prompt(void) {
    char *p = kc_build_prompt("gpt-5.4");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "cutoff");
    ASSERT_STR_CONTAINS(p, KC_GPT54_CUTOFF);
    free(p);
    return 0;
}

static int t_build_prompt_unknown(void) {
    char *p = kc_build_prompt("nope");
    ASSERT_NULL(p);
    return 0;
}

static int t_table_size(void) {
    ASSERT_GT(kc_table_size(), 10);
    return 0;
}

int test_knowledge_cutoff_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("knowledge_cutoff");
    RUN_TEST(t_gpt54);
    RUN_TEST(t_gpt54_mini);
    RUN_TEST(t_opus46);
    RUN_TEST(t_sonnet46);
    RUN_TEST(t_deepseek_r1);
    RUN_TEST(t_qwen3);
    RUN_TEST(t_unknown);
    RUN_TEST(t_null_safety);
    RUN_TEST(t_build_prompt);
    RUN_TEST(t_build_prompt_unknown);
    RUN_TEST(t_table_size);
    TEST_RETURN_FAILED();
}
