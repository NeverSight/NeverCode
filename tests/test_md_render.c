#include "test.h"
#include "nevercode.h"

static int test_md_basic(void) {
    const char *md = "# Title\n## Sub\n- item\n**bold** x\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "Title");
    ASSERT_STR_CONTAINS(t, "## Sub");
    ASSERT_STR_CONTAINS(t, "bold");
    ASSERT_TRUE(strstr(t, "\033[4m") == NULL);
    ASSERT_INT_EQ(md_render_line_count(md), 5);
    free(t);
    return 0;
}

static int test_md_empty(void) {
    ASSERT_INT_EQ(md_render_line_count(""), 0);
    char *t = md_render_to_terminal("");
    ASSERT_NOT_NULL(t);
    free(t);
    return 0;
}

static int test_md_null(void) {
    ASSERT_NULL(md_render_to_terminal(NULL));
    ASSERT_INT_EQ(md_render_line_count(NULL), 0);
    return 0;
}

static int test_md_code_block(void) {
    const char *md = "before\n```c\nint x = 42;\n```\nafter\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "int x = 42");
    ASSERT_STR_CONTAINS(t, "──────────");
    ASSERT_TRUE(strstr(t, "```") == NULL);
    ASSERT_STR_CONTAINS(t, "before");
    ASSERT_STR_CONTAINS(t, "after");
    free(t);
    return 0;
}

static int test_md_inline_code(void) {
    const char *md = "use `foo()` here\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "foo()");
    free(t);
    return 0;
}

static int test_md_italic(void) {
    const char *md = "this is *italic* text\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "italic");
    free(t);
    return 0;
}

static int test_md_blockquote(void) {
    const char *md = "> quoted text\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "quoted text");
    free(t);
    return 0;
}

static int test_md_hr(void) {
    const char *md = "---\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_TRUE(strlen(t) > 3);
    free(t);
    return 0;
}

static int test_md_h3(void) {
    const char *md = "### Section\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "Section");
    free(t);
    return 0;
}

static int test_md_bullet_star(void) {
    const char *md = "* star bullet\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "star bullet");
    free(t);
    return 0;
}

static int test_md_numbered_list(void) {
    const char *md = "1. first\n2. second\n10. tenth\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "first");
    ASSERT_STR_CONTAINS(t, "second");
    ASSERT_STR_CONTAINS(t, "tenth");
    free(t);
    return 0;
}

static int test_md_link(void) {
    const char *md = "see [docs](https://example.com) here\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "docs");
    ASSERT_STR_CONTAINS(t, "example.com");
    ASSERT_TRUE(strstr(t, "\033[4m") == NULL);
    free(t);
    return 0;
}

static int test_md_strikethrough(void) {
    const char *md = "this is ~~deleted~~ text\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "deleted");
    free(t);
    return 0;
}

static int test_md_nested_bullet(void) {
    const char *md = "- top\n  - nested\n";
    char *t = md_render_to_terminal(md);
    ASSERT_NOT_NULL(t);
    ASSERT_STR_CONTAINS(t, "top");
    ASSERT_STR_CONTAINS(t, "\xe2\x97\xa6 nested");
    ASSERT_STR_CONTAINS(t, "nested");
    free(t);
    return 0;
}

int test_md_render_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("md_render");
    RUN_TEST(test_md_basic);
    RUN_TEST(test_md_empty);
    RUN_TEST(test_md_null);
    RUN_TEST(test_md_code_block);
    RUN_TEST(test_md_inline_code);
    RUN_TEST(test_md_italic);
    RUN_TEST(test_md_blockquote);
    RUN_TEST(test_md_hr);
    RUN_TEST(test_md_h3);
    RUN_TEST(test_md_bullet_star);
    RUN_TEST(test_md_numbered_list);
    RUN_TEST(test_md_link);
    RUN_TEST(test_md_strikethrough);
    RUN_TEST(test_md_nested_bullet);
    TEST_RETURN_FAILED();
}
