#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"

static int test_init(void) {
    effort_level_init();
    ASSERT_INT_EQ(effort_level_get(), EFFORT_DEFAULT);
    return 0;
}

static int test_set_get(void) {
    effort_level_set(EFFORT_HIGH);
    ASSERT_INT_EQ(effort_level_get(), EFFORT_HIGH);
    effort_level_set(EFFORT_LOW);
    ASSERT_INT_EQ(effort_level_get(), EFFORT_LOW);
    effort_level_set(EFFORT_XHIGH);
    ASSERT_INT_EQ(effort_level_get(), EFFORT_XHIGH);
    effort_level_set(EFFORT_NONE);
    ASSERT_INT_EQ(effort_level_get(), EFFORT_NONE);
    effort_level_set(EFFORT_MAX);
    ASSERT_INT_EQ(effort_level_get(), EFFORT_MAX);
    effort_level_init();
    return 0;
}

static int test_name(void) {
    ASSERT_STR_EQ(effort_level_name(EFFORT_NONE), "none");
    ASSERT_STR_EQ(effort_level_name(EFFORT_MINIMAL), "minimal");
    ASSERT_STR_EQ(effort_level_name(EFFORT_LOW), "low");
    ASSERT_STR_EQ(effort_level_name(EFFORT_MEDIUM), "medium");
    ASSERT_STR_EQ(effort_level_name(EFFORT_HIGH), "high");
    ASSERT_STR_EQ(effort_level_name(EFFORT_XHIGH), "xhigh");
    ASSERT_STR_EQ(effort_level_name(EFFORT_MAX), "max");
    ASSERT_STR_EQ(effort_level_name(EFFORT_DEFAULT), "default");
    return 0;
}

static int test_parse(void) {
    ASSERT_INT_EQ(effort_level_parse("none"), EFFORT_NONE);
    ASSERT_INT_EQ(effort_level_parse("n"), EFFORT_NONE);
    ASSERT_INT_EQ(effort_level_parse("minimal"), EFFORT_MINIMAL);
    ASSERT_INT_EQ(effort_level_parse("min"), EFFORT_MINIMAL);
    ASSERT_INT_EQ(effort_level_parse("low"), EFFORT_LOW);
    ASSERT_INT_EQ(effort_level_parse("l"), EFFORT_LOW);
    ASSERT_INT_EQ(effort_level_parse("medium"), EFFORT_MEDIUM);
    ASSERT_INT_EQ(effort_level_parse("med"), EFFORT_MEDIUM);
    ASSERT_INT_EQ(effort_level_parse("m"), EFFORT_MEDIUM);
    ASSERT_INT_EQ(effort_level_parse("high"), EFFORT_HIGH);
    ASSERT_INT_EQ(effort_level_parse("h"), EFFORT_HIGH);
    ASSERT_INT_EQ(effort_level_parse("xhigh"), EFFORT_XHIGH);
    ASSERT_INT_EQ(effort_level_parse("xh"), EFFORT_XHIGH);
    ASSERT_INT_EQ(effort_level_parse("max"), EFFORT_MAX);
    ASSERT_INT_EQ(effort_level_parse("default"), EFFORT_DEFAULT);
    ASSERT_INT_EQ(effort_level_parse("d"), EFFORT_DEFAULT);
    ASSERT_INT_EQ(effort_level_parse(NULL), EFFORT_DEFAULT);
    ASSERT_INT_EQ(effort_level_parse("garbage"), EFFORT_DEFAULT);
    return 0;
}

static int test_cycle(void) {
    ASSERT_INT_EQ(effort_level_cycle(EFFORT_DEFAULT), EFFORT_LOW);
    ASSERT_INT_EQ(effort_level_cycle(EFFORT_LOW), EFFORT_MEDIUM);
    ASSERT_INT_EQ(effort_level_cycle(EFFORT_MEDIUM), EFFORT_HIGH);
    ASSERT_INT_EQ(effort_level_cycle(EFFORT_HIGH), EFFORT_XHIGH);
    ASSERT_INT_EQ(effort_level_cycle(EFFORT_XHIGH), EFFORT_DEFAULT);
    ASSERT_INT_EQ(effort_level_cycle(EFFORT_MAX), EFFORT_DEFAULT);
    return 0;
}

static int test_max_tokens(void) {
    ASSERT_INT_EQ(effort_level_max_tokens(EFFORT_NONE, NULL), EFFORT_NONE_TOKENS);
    ASSERT(effort_level_max_tokens(EFFORT_LOW, NULL) > 0);
    ASSERT(effort_level_max_tokens(EFFORT_MEDIUM, NULL) >
           effort_level_max_tokens(EFFORT_LOW, NULL));
    ASSERT(effort_level_max_tokens(EFFORT_HIGH, NULL) >
           effort_level_max_tokens(EFFORT_MEDIUM, NULL));
    ASSERT(effort_level_max_tokens(EFFORT_XHIGH, NULL) >
           effort_level_max_tokens(EFFORT_HIGH, NULL));
    ASSERT(effort_level_max_tokens(EFFORT_MAX, NULL) > effort_level_max_tokens(EFFORT_XHIGH, NULL));
    ASSERT_INT_EQ(effort_level_max_tokens(EFFORT_DEFAULT, NULL), 0);
    return 0;
}

static int test_compat_value(void) {
    ASSERT_STR_EQ(effort_level_compat_value(EFFORT_NONE), "none");
    ASSERT_STR_EQ(effort_level_compat_value(EFFORT_MINIMAL), "minimal");
    ASSERT_STR_EQ(effort_level_compat_value(EFFORT_LOW), "low");
    ASSERT_STR_EQ(effort_level_compat_value(EFFORT_MEDIUM), "medium");
    ASSERT_STR_EQ(effort_level_compat_value(EFFORT_HIGH), "high");
    ASSERT_STR_EQ(effort_level_compat_value(EFFORT_XHIGH), "xhigh");
    ASSERT_NULL(effort_level_compat_value(EFFORT_DEFAULT));
    return 0;
}

static int test_apply_compat_body_high(void) {
    cJSON *body = cJSON_CreateObject();
    effort_apply_to_compat_body(body, EFFORT_HIGH, "gpt-5.4");
    cJSON *re = cJSON_GetObjectItem(body, "reasoning_effort");
    ASSERT_NOT_NULL(re);
    ASSERT_STR_EQ(cJSON_GetStringValue(re), "high");
    cJSON *rs = cJSON_GetObjectItem(body, "reasoning_summary");
    ASSERT_NOT_NULL(rs);
    ASSERT_STR_EQ(cJSON_GetStringValue(rs), "auto");
    cJSON_Delete(body);
    return 0;
}

static int test_apply_compat_body_generic(void) {
    cJSON *body = cJSON_CreateObject();
    effort_apply_to_compat_body(body, EFFORT_LOW, "some-model");
    cJSON *re = cJSON_GetObjectItem(body, "reasoning_effort");
    ASSERT_NOT_NULL(re);
    ASSERT_STR_EQ(cJSON_GetStringValue(re), "low");
    /* Generic models should not get reasoning_summary */
    cJSON *rs = cJSON_GetObjectItem(body, "reasoning_summary");
    ASSERT_NULL(rs);
    cJSON_Delete(body);
    return 0;
}

static int test_apply_compat_body_default_noop(void) {
    cJSON *body = cJSON_CreateObject();
    effort_apply_to_compat_body(body, EFFORT_DEFAULT, "gpt-5.4");
    cJSON *re = cJSON_GetObjectItem(body, "reasoning_effort");
    ASSERT_NULL(re);
    cJSON_Delete(body);
    return 0;
}

static int test_describe(void) {
    effort_level_init();
    char *d = effort_level_describe();
    ASSERT_NOT_NULL(d);
    ASSERT(strstr(d, "default") != NULL);
    free(d);

    effort_level_set(EFFORT_HIGH);
    d = effort_level_describe();
    ASSERT_NOT_NULL(d);
    ASSERT(strstr(d, "high") != NULL);
    free(d);

    effort_level_set(EFFORT_XHIGH);
    d = effort_level_describe();
    ASSERT_NOT_NULL(d);
    ASSERT(strstr(d, "xhigh") != NULL);
    free(d);

    effort_level_init();
    return 0;
}

static int test_glyph_non_null(void) {
    ASSERT_NOT_NULL(effort_level_glyph(EFFORT_NONE));
    ASSERT_NOT_NULL(effort_level_glyph(EFFORT_LOW));
    ASSERT_NOT_NULL(effort_level_glyph(EFFORT_MEDIUM));
    ASSERT_NOT_NULL(effort_level_glyph(EFFORT_HIGH));
    ASSERT_NOT_NULL(effort_level_glyph(EFFORT_MAX));
    ASSERT_NOT_NULL(effort_level_glyph(EFFORT_DEFAULT));
    return 0;
}

static int test_glyph_distinct_levels(void) {
    const char *low = effort_level_glyph(EFFORT_LOW);
    const char *high = effort_level_glyph(EFFORT_HIGH);
    const char *max = effort_level_glyph(EFFORT_MAX);
    ASSERT_TRUE(strcmp(low, high) != 0 || strcmp(high, max) != 0);
    return 0;
}

int test_effort_level_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("effort_level");

    RUN_TEST(test_init);
    RUN_TEST(test_set_get);
    RUN_TEST(test_name);
    RUN_TEST(test_parse);
    RUN_TEST(test_cycle);
    RUN_TEST(test_max_tokens);
    RUN_TEST(test_compat_value);
    RUN_TEST(test_apply_compat_body_high);
    RUN_TEST(test_apply_compat_body_generic);
    RUN_TEST(test_apply_compat_body_default_noop);
    RUN_TEST(test_describe);
    RUN_TEST(test_glyph_non_null);
    RUN_TEST(test_glyph_distinct_levels);

    TEST_RETURN_FAILED();
}
