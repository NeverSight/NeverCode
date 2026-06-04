#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"

static int test_init(void) {
    prompt_suggest_init(false);
    ASSERT(!prompt_suggest_enabled());
    ASSERT_NULL(prompt_suggest_current());

    prompt_suggest_init(true);
    ASSERT(prompt_suggest_enabled());
    return 0;
}

static int test_enable_disable(void) {
    prompt_suggest_init(true);
    ASSERT(prompt_suggest_enabled());

    prompt_suggest_set_enabled(false);
    ASSERT(!prompt_suggest_enabled());

    prompt_suggest_set_enabled(true);
    ASSERT(prompt_suggest_enabled());
    return 0;
}

static int test_clear(void) {
    prompt_suggest_init(true);
    prompt_suggest_clear();
    ASSERT_NULL(prompt_suggest_current());
    return 0;
}

static int test_filter_pass(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("run the tests"), FILTER_PASS);
    ASSERT_INT_EQ(prompt_suggest_filter("commit this"), FILTER_PASS);
    ASSERT_INT_EQ(prompt_suggest_filter("push it"), FILTER_PASS);
    ASSERT_INT_EQ(prompt_suggest_filter("try it out"), FILTER_PASS);
    return 0;
}

static int test_filter_empty(void) {
    ASSERT_INT_EQ(prompt_suggest_filter(NULL), FILTER_EMPTY);
    ASSERT_INT_EQ(prompt_suggest_filter(""), FILTER_EMPTY);
    return 0;
}

static int test_filter_meta(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("done"), FILTER_META);
    ASSERT_INT_EQ(prompt_suggest_filter("nothing to suggest"), FILTER_META);
    ASSERT_INT_EQ(prompt_suggest_filter("No suggestion needed"), FILTER_META);
    ASSERT_INT_EQ(prompt_suggest_filter("(silence)"), FILTER_META);
    ASSERT_INT_EQ(prompt_suggest_filter("[no suggestion]"), FILTER_META);
    return 0;
}

static int test_filter_error(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("API error: timeout"), FILTER_ERROR);
    ASSERT_INT_EQ(prompt_suggest_filter("prompt is too long"), FILTER_ERROR);
    return 0;
}

static int test_filter_prefix(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("Suggestion: do something"), FILTER_PREFIX);
    return 0;
}

static int test_filter_too_short(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("banana"), FILTER_TOO_SHORT);
    ASSERT_INT_EQ(prompt_suggest_filter("hmm"), FILTER_TOO_SHORT);
    return 0;
}

static int test_filter_allowed_single(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("yes"), FILTER_PASS);
    ASSERT_INT_EQ(prompt_suggest_filter("ok"), FILTER_PASS);
    ASSERT_INT_EQ(prompt_suggest_filter("commit"), FILTER_PASS);
    ASSERT_INT_EQ(prompt_suggest_filter("continue"), FILTER_PASS);
    ASSERT_INT_EQ(prompt_suggest_filter("no"), FILTER_PASS);
    return 0;
}

static int test_filter_slash_single(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("/help"), FILTER_PASS);
    ASSERT_INT_EQ(prompt_suggest_filter("/commit"), FILTER_PASS);
    return 0;
}

static int test_filter_too_long(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("this is a very long suggestion that has way too many "
                                        "words in it to be useful for the user"),
                  FILTER_TOO_LONG);
    return 0;
}

static int test_filter_multi_sentence(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("Do this. Then do that."), FILTER_MULTI_SENTENCE);
    return 0;
}

static int test_filter_formatting(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("line1\nline2"), FILTER_FORMATTING);
    ASSERT_INT_EQ(prompt_suggest_filter("**bold text**"), FILTER_FORMATTING);
    return 0;
}

static int test_filter_evaluative(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("looks good to me"), FILTER_EVALUATIVE);
    ASSERT_INT_EQ(prompt_suggest_filter("thanks for that"), FILTER_EVALUATIVE);
    ASSERT_INT_EQ(prompt_suggest_filter("that works well"), FILTER_EVALUATIVE);
    return 0;
}

static int test_filter_ai_voice(void) {
    ASSERT_INT_EQ(prompt_suggest_filter("Let me check that"), FILTER_AI_VOICE);
    ASSERT_INT_EQ(prompt_suggest_filter("I'll fix that now"), FILTER_AI_VOICE);
    ASSERT_INT_EQ(prompt_suggest_filter("Here's the plan"), FILTER_AI_VOICE);
    ASSERT_INT_EQ(prompt_suggest_filter("You should try this"), FILTER_AI_VOICE);
    return 0;
}

static int test_filter_name(void) {
    ASSERT_STR_EQ(prompt_filter_name(FILTER_PASS), "pass");
    ASSERT_STR_EQ(prompt_filter_name(FILTER_EMPTY), "empty");
    ASSERT_STR_EQ(prompt_filter_name(FILTER_META), "meta_text");
    ASSERT_STR_EQ(prompt_filter_name(FILTER_ERROR), "error_message");
    ASSERT_STR_EQ(prompt_filter_name(FILTER_EVALUATIVE), "evaluative");
    ASSERT_STR_EQ(prompt_filter_name(FILTER_AI_VOICE), "ai_voice");
    return 0;
}

static int test_prompt_text(void) {
    const char *p = prompt_suggest_prompt_text();
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "SUGGESTION MODE") != NULL);
    ASSERT(strstr(p, "NEVER SUGGEST") != NULL);
    ASSERT(strstr(p, "2-12 words") != NULL);
    return 0;
}

static int test_generate_null_conv(void) {
    prompt_suggest_init(true);
    char *r = prompt_suggest_generate(NULL, "http://test", "key", "model", PROVIDER_COMPAT);
    ASSERT_NULL(r);
    return 0;
}

static int test_generate_disabled(void) {
    prompt_suggest_init(false);
    char *r = prompt_suggest_generate(NULL, "http://test", "key", "model", PROVIDER_COMPAT);
    ASSERT_NULL(r);
    return 0;
}

int test_prompt_suggest_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("prompt_suggest");

    RUN_TEST(test_init);
    RUN_TEST(test_enable_disable);
    RUN_TEST(test_clear);
    RUN_TEST(test_filter_pass);
    RUN_TEST(test_filter_empty);
    RUN_TEST(test_filter_meta);
    RUN_TEST(test_filter_error);
    RUN_TEST(test_filter_prefix);
    RUN_TEST(test_filter_too_short);
    RUN_TEST(test_filter_allowed_single);
    RUN_TEST(test_filter_slash_single);
    RUN_TEST(test_filter_too_long);
    RUN_TEST(test_filter_multi_sentence);
    RUN_TEST(test_filter_formatting);
    RUN_TEST(test_filter_evaluative);
    RUN_TEST(test_filter_ai_voice);
    RUN_TEST(test_filter_name);
    RUN_TEST(test_prompt_text);
    RUN_TEST(test_generate_null_conv);
    RUN_TEST(test_generate_disabled);

    TEST_RETURN_FAILED();
}
