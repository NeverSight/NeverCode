#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_set_get(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    ASSERT_INT_EQ(prompt_ctx_set(&pc, "foo", "bar"), 0);
    ASSERT_STR_EQ(prompt_ctx_get(&pc, "foo"), "bar");
    ASSERT_NULL(prompt_ctx_get(&pc, "missing"));
    prompt_ctx_free(&pc);
    return 0;
}

static int t_overwrite(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    prompt_ctx_set(&pc, "k", "v1");
    prompt_ctx_set(&pc, "k", "v2");
    ASSERT_STR_EQ(prompt_ctx_get(&pc, "k"), "v2");
    ASSERT_INT_EQ(pc.nvars, 1);
    prompt_ctx_free(&pc);
    return 0;
}

static int t_render_simple(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    prompt_ctx_set(&pc, "name", "world");

    char *out = prompt_ctx_render(&pc, "hello {{ name }}!");
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "hello world!");
    free(out);
    prompt_ctx_free(&pc);
    return 0;
}

static int t_render_missing_var(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    char *out = prompt_ctx_render(&pc, "hi {{unknown}} there");
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "{{unknown}}");
    free(out);
    prompt_ctx_free(&pc);
    return 0;
}

static int t_render_no_templates(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    char *out = prompt_ctx_render(&pc, "plain text");
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "plain text");
    free(out);
    prompt_ctx_free(&pc);
    return 0;
}

static int t_render_multiple(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    prompt_ctx_set(&pc, "a", "X");
    prompt_ctx_set(&pc, "b", "Y");
    char *out = prompt_ctx_render(&pc, "{{a}}+{{b}}");
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "X+Y");
    free(out);
    prompt_ctx_free(&pc);
    return 0;
}

static int t_null_render(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    ASSERT_NULL(prompt_ctx_render(&pc, NULL));
    prompt_ctx_free(&pc);
    return 0;
}

static int t_empty_key_rejected(void) {
    prompt_ctx_t pc;
    prompt_ctx_init(&pc);
    ASSERT_INT_EQ(prompt_ctx_set(&pc, "", "val"), -1);
    ASSERT_INT_EQ(prompt_ctx_set(&pc, NULL, "val"), -1);
    prompt_ctx_free(&pc);
    return 0;
}

int test_prompt_ctx_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("prompt_ctx");
    RUN_TEST(t_set_get);
    RUN_TEST(t_overwrite);
    RUN_TEST(t_render_simple);
    RUN_TEST(t_render_missing_var);
    RUN_TEST(t_render_no_templates);
    RUN_TEST(t_render_multiple);
    RUN_TEST(t_null_render);
    RUN_TEST(t_empty_key_rejected);
    TEST_RETURN_FAILED();
}
