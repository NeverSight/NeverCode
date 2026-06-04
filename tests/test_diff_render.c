#include "test.h"
#include "nevercode.h"

static int test_dr_colorize(void) {
    const char *d = "--- a\n+++ b\n@@ -1 +1 @@\n-old\n+new\n";
    char *c = diff_render_colorize(d);
    ASSERT_NOT_NULL(c);
    ASSERT_STR_CONTAINS(c, "old");
    ASSERT_STR_CONTAINS(c, "new");
    free(c);
    return 0;
}

static int test_dr_stat_count(void) {
    const char *d = "+a\n+b\n-c\n";
    int a = 0, r = 0;
    ASSERT_INT_EQ(diff_render_count_changes(d, &a, &r), 0);
    ASSERT_INT_EQ(a, 2);
    ASSERT_INT_EQ(r, 1);
    char *s = diff_render_stat(d);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "+2");
    free(s);
    return 0;
}

static int test_dr_null(void) {
    ASSERT_NULL(diff_render_colorize(NULL));
    ASSERT_INT_EQ(diff_render_count_changes(NULL, NULL, NULL), -1);
    ASSERT_INT_EQ(diff_render_file_count(NULL), 0);
    return 0;
}

static int test_dr_extract_files(void) {
    const char *d = "diff --git a/foo.c b/foo.c\n"
                    "--- a/foo.c\n"
                    "+++ b/foo.c\n"
                    "@@ -1 +1 @@\n"
                    "-old\n"
                    "+new\n"
                    "diff --git a/bar.h b/bar.h\n"
                    "--- a/bar.h\n"
                    "+++ b/bar.h\n"
                    "+added\n";
    const char *out[8];
    int n = diff_render_extract_files(d, out, 8);
    ASSERT_INT_EQ(n, 2);
    ASSERT_STR_EQ(out[0], "foo.c");
    ASSERT_STR_EQ(out[1], "bar.h");
    for (int i = 0; i < n; i++)
        free((void *)out[i]);
    return 0;
}

static int test_dr_file_count(void) {
    const char *d = "+++ b/a.c\n+++ b/b.c\n+++ b/c.c\n";
    ASSERT_INT_EQ(diff_render_file_count(d), 3);
    return 0;
}

static int test_dr_summary(void) {
    const char *d = "+++ b/x.c\n+a\n+b\n-c\n";
    char *s = diff_render_summary(d);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "1 file(s)");
    ASSERT_STR_CONTAINS(s, "2 insertion(s)");
    ASSERT_STR_CONTAINS(s, "1 deletion(s)");
    free(s);
    return 0;
}

static int test_dr_diff_header(void) {
    const char *d = "diff --git a/f b/f\n--- a/f\n+++ b/f\n";
    char *c = diff_render_colorize(d);
    ASSERT_NOT_NULL(c);
    ASSERT_STR_CONTAINS(c, "\033[1m");
    free(c);
    return 0;
}

static int test_dr_word_highlight_basic(void) {
    char *r = diff_render_word_highlight("hello world", "hello earth");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "world");
    ASSERT_STR_CONTAINS(r, "earth");
    free(r);
    return 0;
}

static int test_dr_word_highlight_null(void) {
    ASSERT_NULL(diff_render_word_highlight(NULL, "test"));
    ASSERT_NULL(diff_render_word_highlight("test", NULL));
    return 0;
}

static int test_dr_word_highlight_identical(void) {
    char *r = diff_render_word_highlight("same text", "same text");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "same");
    free(r);
    return 0;
}

static int test_dr_cfg_macros(void) {
    ASSERT_STR_EQ(NC_CFG_MCP, "mcp.json");
    ASSERT_STR_EQ(NC_CFG_CONFIG, "config.json");
    ASSERT_STR_EQ(NC_CFG_NETWORK, "network.json");
    ASSERT_STR_EQ(NC_CFG_HOOKS, "hooks.json");
    ASSERT_STR_EQ(NC_CFG_MEMORY, "memory.json");
    ASSERT_STR_EQ(NC_CFG_SETTINGS, "settings.json");
    return 0;
}

int test_diff_render_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("diff_render");
    RUN_TEST(test_dr_colorize);
    RUN_TEST(test_dr_stat_count);
    RUN_TEST(test_dr_null);
    RUN_TEST(test_dr_extract_files);
    RUN_TEST(test_dr_file_count);
    RUN_TEST(test_dr_summary);
    RUN_TEST(test_dr_diff_header);
    RUN_TEST(test_dr_word_highlight_basic);
    RUN_TEST(test_dr_word_highlight_null);
    RUN_TEST(test_dr_word_highlight_identical);
    RUN_TEST(test_dr_cfg_macros);
    TEST_RETURN_FAILED();
}
