#include "test.h"
#include "nevercode.h"

static int t_claude3_opus(void) {
    ASSERT_TRUE(depr_is_deprecated("claude-3-opus-20240229"));
    ASSERT_NOT_NULL(depr_display_name("claude-3-opus-20240229"));
    ASSERT_STR_CONTAINS(depr_display_name("claude-3-opus-20240229"), "Claude 3 Opus");
    return 0;
}

static int t_claude37_sonnet(void) {
    ASSERT_TRUE(depr_is_deprecated("claude-3-7-sonnet-latest"));
    ASSERT_NOT_NULL(depr_retirement_date("claude-3-7-sonnet-latest"));
    return 0;
}

static int t_gpt4_turbo(void) {
    ASSERT_TRUE(depr_is_deprecated("gpt-4-turbo-preview"));
    ASSERT_STR_CONTAINS(depr_display_name("gpt-4-turbo-preview"), "GPT-4 Turbo");
    return 0;
}

static int t_not_deprecated(void) {
    ASSERT_FALSE(depr_is_deprecated("gpt-5.4"));
    ASSERT_FALSE(depr_is_deprecated("claude-opus-4-6"));
    ASSERT_FALSE(depr_is_deprecated("claude-sonnet-4-6"));
    return 0;
}

static int t_null_safety(void) {
    ASSERT_FALSE(depr_is_deprecated(NULL));
    ASSERT_FALSE(depr_is_deprecated(""));
    ASSERT_NULL(depr_display_name(NULL));
    ASSERT_NULL(depr_retirement_date(NULL));
    ASSERT_NULL(depr_warning_message(NULL));
    return 0;
}

static int t_warning_message(void) {
    char *msg = depr_warning_message("claude-3-opus-20240229");
    ASSERT_NOT_NULL(msg);
    ASSERT_STR_CONTAINS(msg, "deprecated");
    ASSERT_STR_CONTAINS(msg, "retired");
    free(msg);
    return 0;
}

static int t_warning_not_deprecated(void) {
    char *msg = depr_warning_message("gpt-5.4");
    ASSERT_NULL(msg);
    return 0;
}

static int t_table_size(void) {
    ASSERT_GT(depr_table_size(), 3);
    return 0;
}

static int t_case_insensitive(void) {
    ASSERT_TRUE(depr_is_deprecated("Claude-3-Opus-20240229"));
    ASSERT_TRUE(depr_is_deprecated("CLAUDE-3-OPUS"));
    return 0;
}

int test_model_deprecation_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("model_deprecation");
    RUN_TEST(t_claude3_opus);
    RUN_TEST(t_claude37_sonnet);
    RUN_TEST(t_gpt4_turbo);
    RUN_TEST(t_not_deprecated);
    RUN_TEST(t_null_safety);
    RUN_TEST(t_warning_message);
    RUN_TEST(t_warning_not_deprecated);
    RUN_TEST(t_table_size);
    RUN_TEST(t_case_insensitive);
    TEST_RETURN_FAILED();
}
