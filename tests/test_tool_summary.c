#include "test.h"
#include "nevercode.h"
static int t1(void) {
    const char *n = "bash", *i = "{\"command\":\"ls\"}";
    char *s = tool_batch_summarize_single(n, i, NULL);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Ran");
    free(s);
    return 0;
}
static int t2(void) {
    const char *n = "file_read", *i = "{\"path\":\"/foo/bar.c\"}";
    char *s = tool_batch_summarize_single(n, i, NULL);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "bar.c");
    free(s);
    return 0;
}
static int t3(void) {
    const char *n = "file_write", *i = "{\"path\":\"/src/main.c\"}";
    char *s = tool_batch_summarize_single(n, i, NULL);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "main.c");
    free(s);
    return 0;
}
static int t4(void) {
    const char *n = "file_edit", *i = "{\"file_path\":\"/src/x.h\"}";
    char *s = tool_batch_summarize_single(n, i, NULL);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "x.h");
    free(s);
    return 0;
}
static int t5(void) {
    const char *n = "grep", *i = "{\"pattern\":\"TODO\"}";
    char *s = tool_batch_summarize_single(n, i, NULL);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "TODO");
    free(s);
    return 0;
}
static int t6(void) {
    const char *n = "glob";
    char *s = tool_batch_summarize_single(n, "{}", NULL);
    ASSERT_NOT_NULL(s);
    free(s);
    return 0;
}
static int t7(void) {
    const char *n = "custom_tool";
    char *s = tool_batch_summarize_single(n, "{}", NULL);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Custom tool");
    free(s);
    return 0;
}
static int t8(void) {
    const char *ns[] = {"file_read", "file_edit", "bash"};
    char *s = tool_batch_summarize(ns, NULL, NULL, 3);
    ASSERT_NOT_NULL(s);
    free(s);
    return 0;
}
static int t9(void) {
    char *s = tool_batch_summarize(NULL, NULL, NULL, 0);
    ASSERT_NULL(s);
    return 0;
}
static int ta(void) {
    char *s = tool_summary_truncate_json(NULL, 100);
    ASSERT_STR_EQ(s, "[null]");
    free(s);
    return 0;
}
static int tb(void) {
    char *s = tool_summary_truncate_json("hi", 100);
    ASSERT_STR_EQ(s, "hi");
    free(s);
    return 0;
}
int test_tool_summary_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_summary");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(ta);
    RUN_TEST(tb);
    TEST_RETURN_FAILED();
}
