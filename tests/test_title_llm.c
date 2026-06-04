/*
 * test_title_llm.c — Tests for LLM-driven session title generation.
 */
#include "test.h"
#include "nevercode.h"

static int t_initially_empty(void) {
    title_llm_reset();
    ASSERT_NULL(title_llm_get());
    ASSERT(!title_llm_is_ready());
    return 0;
}

static int t_set_and_get(void) {
    title_llm_reset();
    title_llm_set("Fix memory leak in parser");
    ASSERT(title_llm_is_ready());
    ASSERT_STR_CONTAINS(title_llm_get(), "Fix memory");
    title_llm_reset();
    ASSERT_NULL(title_llm_get());
    return 0;
}

static int t_null_set(void) {
    title_llm_reset();
    title_llm_set(NULL);
    ASSERT_NULL(title_llm_get());
    return 0;
}

static int t_macros(void) {
    ASSERT(TITLE_LLM_MAX > 0);
    return 0;
}

int test_title_llm_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("title_llm");
    RUN_TEST(t_initially_empty);
    RUN_TEST(t_set_and_get);
    RUN_TEST(t_null_set);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
