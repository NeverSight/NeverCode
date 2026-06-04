#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"
#include <unistd.h>

static int test_init(void) {
    away_summary_init();
    ASSERT(away_summary_enabled());
    ASSERT(away_summary_idle_seconds() >= 0);
    ASSERT(away_summary_idle_seconds() < 2);
    ASSERT(!away_summary_should_generate());
    return 0;
}

static int test_touch_resets_idle(void) {
    away_summary_init();
    sleep(1);
    long before = away_summary_idle_seconds();
    ASSERT(before >= 1);
    away_summary_touch();
    long after = away_summary_idle_seconds();
    ASSERT(after < before);
    return 0;
}

static int test_enable_disable(void) {
    away_summary_init();
    ASSERT(away_summary_enabled());

    away_summary_set_enabled(false);
    ASSERT(!away_summary_enabled());
    ASSERT(!away_summary_should_generate());

    away_summary_set_enabled(true);
    ASSERT(away_summary_enabled());
    return 0;
}

static int test_should_not_generate_early(void) {
    away_summary_init();
    ASSERT(!away_summary_should_generate());
    return 0;
}

static int test_format_null(void) {
    char *r = away_summary_format(NULL);
    ASSERT_NULL(r);
    return 0;
}

static int test_format_empty(void) {
    char *r = away_summary_format("");
    ASSERT_NULL(r);
    return 0;
}

static int test_format_valid(void) {
    char *r = away_summary_format("Working on API refactoring. Next: add tests.");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "While you were away") != NULL);
    ASSERT(strstr(r, "API refactoring") != NULL);
    free(r);
    return 0;
}

static int test_prompt_text(void) {
    const char *p = away_summary_prompt_text();
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "stepped away") != NULL);
    ASSERT(strstr(p, "1-3 short sentences") != NULL);
    return 0;
}

static int test_generate_null_conv(void) {
    char *r = away_summary_generate(NULL, NULL, "http://test", "key", "model", PROVIDER_COMPAT);
    ASSERT_NULL(r);
    return 0;
}

static int test_disabled_blocks_generate(void) {
    away_summary_init();
    away_summary_set_enabled(false);
    ASSERT(!away_summary_should_generate());
    return 0;
}

int test_away_summary_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("away_summary");

    RUN_TEST(test_init);
    RUN_TEST(test_touch_resets_idle);
    RUN_TEST(test_enable_disable);
    RUN_TEST(test_should_not_generate_early);
    RUN_TEST(test_format_null);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_format_valid);
    RUN_TEST(test_prompt_text);
    RUN_TEST(test_generate_null_conv);
    RUN_TEST(test_disabled_blocks_generate);

    TEST_RETURN_FAILED();
}
