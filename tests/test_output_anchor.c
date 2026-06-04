#include "test.h"
#include "nevercode.h"

static int t_defaults(void) {
    oa_init();
    ASSERT_TRUE(oa_enabled());
    ASSERT_INT_EQ(oa_inter_tool_limit(), OA_INTER_TOOL_WORDS);
    ASSERT_INT_EQ(oa_final_response_limit(), OA_FINAL_RESPONSE_WORDS);
    return 0;
}

static int t_build_enabled(void) {
    oa_init();
    char *s = oa_build_section();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "25 words");
    ASSERT_STR_CONTAINS(s, "100 words");
    free(s);
    return 0;
}

static int t_build_disabled(void) {
    oa_init();
    oa_set_enabled(false);
    ASSERT_FALSE(oa_enabled());
    char *s = oa_build_section();
    ASSERT_NULL(s);
    return 0;
}

static int t_custom_limits(void) {
    oa_init();
    oa_set_limits(50, 200);
    ASSERT_INT_EQ(oa_inter_tool_limit(), 50);
    ASSERT_INT_EQ(oa_final_response_limit(), 200);
    char *s = oa_build_section();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "50 words");
    ASSERT_STR_CONTAINS(s, "200 words");
    free(s);
    return 0;
}

static int t_macros(void) {
    ASSERT_GT(OA_INTER_TOOL_WORDS, 0);
    ASSERT_GT(OA_FINAL_RESPONSE_WORDS, 0);
    return 0;
}

int test_output_anchor_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("output_anchor");
    RUN_TEST(t_defaults);
    RUN_TEST(t_build_enabled);
    RUN_TEST(t_build_disabled);
    RUN_TEST(t_custom_limits);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
