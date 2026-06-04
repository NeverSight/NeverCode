#include "test.h"
#include "nevercode.h"

static int test_ot_null(void) {
    ASSERT_NULL(output_trunc_lines(NULL, 2, 2));
    ASSERT_INT_EQ(output_trunc_count_lines(NULL), 0);
    return 0;
}

static int test_ot_small(void) {
    const char *t = "one\ntwo\nthree\n";
    char *x = output_trunc_lines(t, 10, 10);
    ASSERT_NOT_NULL(x);
    ASSERT_STR_EQ(x, t);
    free(x);
    ASSERT_INT_EQ(output_trunc_count_lines(t), 3);
    return 0;
}

static int test_ot_omits_middle(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < 50; i++)
        strbuf_appendf(&sb, "line %d\n", i);
    char *big = strbuf_detach(&sb);
    ASSERT_NOT_NULL(big);
    char *cut = output_trunc_lines(big, 5, 5);
    ASSERT_NOT_NULL(cut);
    ASSERT_STR_CONTAINS(cut, "omitted");
    ASSERT_STR_CONTAINS(cut, "line 0");
    ASSERT_STR_CONTAINS(cut, "line 49");
    free(cut);
    free(big);
    return 0;
}

static int test_ot_tool_result(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < 400; i++)
        strbuf_appendf(&sb, "L%d\n", i);
    char *big = strbuf_detach(&sb);
    /* max_bytes above strlen(big) skips line-aware path; keep cap low to force truncation */
    char *tr = output_trunc_tool_result(big, "bash", 800);
    ASSERT_NOT_NULL(tr);
    ASSERT(strlen(tr) < strlen(big));
    free(tr);
    free(big);
    return 0;
}

int test_output_trunc_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("output_trunc");
    RUN_TEST(test_ot_null);
    RUN_TEST(test_ot_small);
    RUN_TEST(test_ot_omits_middle);
    RUN_TEST(test_ot_tool_result);
    TEST_RETURN_FAILED();
}
