#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include <stdio.h>
#include <string.h>

extern bool g_plan_mode;
extern bool g_brief_mode;
extern memory_ctx_t g_memory;
extern char *build_system_prompt(void);

static int test_prompt_contains_environment(void) {
    tools_register_all();
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "Working directory:") != NULL);
    ASSERT(strstr(prompt, "OS:") != NULL);
    ASSERT(strstr(prompt, "Date:") != NULL);
    free(prompt);
    return 0;
}

static int test_prompt_plan_mode_indicator(void) {
    g_plan_mode = true;
    g_brief_mode = false;
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "Plan mode") != NULL);
    ASSERT(strstr(prompt, "read-only") != NULL);
    free(prompt);
    g_plan_mode = false;
    return 0;
}

static int test_prompt_brief_mode_indicator(void) {
    g_plan_mode = false;
    g_brief_mode = true;
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "Brief mode") != NULL);
    free(prompt);
    g_brief_mode = false;
    return 0;
}

static int test_prompt_no_mode_when_off(void) {
    g_plan_mode = false;
    g_brief_mode = false;
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "Active Modes") == NULL);
    free(prompt);
    return 0;
}

static int test_prompt_memory_context(void) {
    memory_init(&g_memory);
    memory_set(&g_memory, "test_pref", "user likes Rust");
    char *mem_str = memory_format_for_prompt(&g_memory);
    ASSERT_NOT_NULL(mem_str);
    ASSERT(strstr(mem_str, "user likes Rust") != NULL);
    free(mem_str);
    memory_free(&g_memory);
    memory_init(&g_memory);
    return 0;
}

static int test_prompt_tools_listed(void) {
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "Available Tools") != NULL);
    ASSERT(strstr(prompt, "Bash") != NULL);
    free(prompt);
    return 0;
}

int test_prompt_enhanced_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("prompt_enhanced");

    RUN_TEST(test_prompt_contains_environment);
    RUN_TEST(test_prompt_plan_mode_indicator);
    RUN_TEST(test_prompt_brief_mode_indicator);
    RUN_TEST(test_prompt_no_mode_when_off);
    RUN_TEST(test_prompt_memory_context);
    RUN_TEST(test_prompt_tools_listed);

    TEST_RETURN_FAILED();
}
