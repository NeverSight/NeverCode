#include "test.h"
#include "terminal.h"
static int test_is_tty(void) {
    (void)term_is_tty();
    return 0;
}
static int test_width(void) {
    int w = term_width();
    ASSERT_GT(w, 0);
    return 0;
}
static int test_slash_cmds(void) {
    const char **c = term_get_slash_cmds();
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(c[0]);
    return 0;
}
static int test_no_md(void) {
    term_set_no_markdown(true);
    term_set_no_markdown(false);
    return 0;
}
static int test_title_set_clear(void) {
    term_title_set("test %d", 42);
    term_title_clear();
    term_title_clear(); /* double clear is safe */
    return 0;
}
static int test_title_sanitize(void) {
    term_title_set("safe\x1b[31m\x07\x9ctext");
    term_title_clear();
    return 0;
}
static int test_stream_partial_line(void) {
    term_stream_start();
    term_stream_text("hello");
    term_stream_text(" world");
    term_stream_text("\nnext line");
    term_stream_end();
    return 0;
}
static int test_td_cols_rows(void) {
    int c = td_cols();
    int r = td_rows();
    ASSERT(c > 0);
    ASSERT(r > 0);
    return 0;
}
static int test_td_bool_apis(void) {
    (void)td_has_truecolor();
    (void)td_is_tmux();
    (void)td_is_ssh();
    (void)td_is_dumb();
    (void)td_has_unicode();
    return 0;
}
int test_terminal_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("terminal");
    RUN_TEST(test_is_tty);
    RUN_TEST(test_width);
    RUN_TEST(test_slash_cmds);
    RUN_TEST(test_no_md);
    RUN_TEST(test_title_set_clear);
    RUN_TEST(test_title_sanitize);
    RUN_TEST(test_stream_partial_line);
    RUN_TEST(test_td_cols_rows);
    RUN_TEST(test_td_bool_apis);
    TEST_RETURN_FAILED();
}
