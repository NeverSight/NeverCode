#include "nevercode.h"
#include "repl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN(name)                                                                                  \
    do {                                                                                           \
        printf("  %-50s ", #name);                                                                 \
        tests_run++;                                                                               \
        name();                                                                                    \
        tests_passed++;                                                                            \
        printf("\033[32mPASS\033[0m\n");                                                           \
    } while (0)

static void test_init_free(void) {
    session_memory_init();
    assert(session_memory_enabled());
    assert(session_memory_content() == NULL);
    assert(session_memory_extraction_count() == 0);
    session_memory_free();
}

static void test_enable_disable(void) {
    session_memory_init();
    assert(session_memory_enabled());
    session_memory_set_enabled(false);
    assert(!session_memory_enabled());
    session_memory_set_enabled(true);
    assert(session_memory_enabled());
    session_memory_free();
}

static void test_should_extract_disabled(void) {
    session_memory_init();
    session_memory_set_enabled(false);
    assert(!session_memory_should_extract(10));
    session_memory_free();
}

static void test_should_extract_too_early(void) {
    session_memory_init();
    session_memory_mark_extracted(5);
    assert(!session_memory_should_extract(6));
    assert(!session_memory_should_extract(7));
    session_memory_free();
}

static void test_should_extract_not_enough_tools(void) {
    session_memory_init();
    session_memory_mark_extracted(0);
    session_memory_record_tool_call();
    assert(!session_memory_should_extract(5));
    session_memory_free();
}

static void test_should_extract_ready(void) {
    session_memory_init();
    session_memory_mark_extracted(0);
    session_memory_record_tool_call();
    session_memory_record_tool_call();
    session_memory_record_tool_call();
    assert(session_memory_should_extract(5));
    session_memory_free();
}

static void test_for_compact_empty(void) {
    session_memory_init();
    char *ctx = session_memory_for_compact();
    assert(ctx == NULL);
    session_memory_free();
}

static void test_for_prompt_empty(void) {
    session_memory_init();
    char *p = session_memory_for_prompt();
    assert(p == NULL);
    session_memory_free();
}

static void test_set_session(void) {
    session_memory_init();
    session_memory_set_session("test-session-123");
    session_memory_set_session(NULL);
    session_memory_set_session("another-session");
    session_memory_free();
}

static void test_extraction_count(void) {
    session_memory_init();
    assert(session_memory_extraction_count() == 0);
    session_memory_free();
}

static void test_save_no_content(void) {
    session_memory_init();
    int rc = session_memory_save();
    assert(rc == -1);
    session_memory_free();
}

static void test_load_nonexistent(void) {
    session_memory_init();
    session_memory_set_session("nonexistent-session-xyz");
    int rc = session_memory_load();
    assert(rc == -1);
    session_memory_free();
}

static void test_model_context_gpt5(void) {
    assert(context_limit_for_model("gpt-5.4") == 1048576);
    assert(context_limit_for_model("gpt-5-turbo") == 1048576);
}

static void test_model_context_gpt4_1(void) {
    assert(context_limit_for_model("gpt-4.1-turbo") == 1048576);
    assert(context_limit_for_model("gpt-4.1") == 1048576);
}

static void test_model_context_compat(void) {
    assert(context_limit_for_model("gpt-4o-mini") == 128000);
    assert(context_limit_for_model("o3-mini") == 200000);
    assert(context_limit_for_model("o4-mini-high") == 200000);
    assert(context_limit_for_model("o1-preview") == 200000);
}

static void test_model_context_large_ctx(void) {
    assert(context_limit_for_model("claude-4-opus") == 200000);
    assert(context_limit_for_model("claude-3.5-sonnet") == 200000);
}

static void test_model_context_gemini(void) {
    assert(context_limit_for_model("gemini-2.5-pro") == 1048576);
    assert(context_limit_for_model("gemini-2.0-flash") == 1048576);
}

static void test_model_context_others(void) {
    assert(context_limit_for_model("deepseek-coder") == 128000);
    assert(context_limit_for_model("qwen-72b") == 131072);
    assert(context_limit_for_model("mistral-large") == 128000);
    assert(context_limit_for_model("unknown-model") == 128000);
    assert(context_limit_for_model(NULL) == 128000);
}

int test_session_memory_all(void) {
    tests_run = 0;
    tests_passed = 0;
    printf("\n\033[1m[session_memory]\033[0m\n");
    RUN(test_init_free);
    RUN(test_enable_disable);
    RUN(test_should_extract_disabled);
    RUN(test_should_extract_too_early);
    RUN(test_should_extract_not_enough_tools);
    RUN(test_should_extract_ready);
    RUN(test_for_compact_empty);
    RUN(test_for_prompt_empty);
    RUN(test_set_session);
    RUN(test_extraction_count);
    RUN(test_save_no_content);
    RUN(test_load_nonexistent);
    RUN(test_model_context_gpt5);
    RUN(test_model_context_gpt4_1);
    RUN(test_model_context_compat);
    RUN(test_model_context_large_ctx);
    RUN(test_model_context_gemini);
    RUN(test_model_context_others);
    return tests_passed == tests_run ? 0 : 1;
}
