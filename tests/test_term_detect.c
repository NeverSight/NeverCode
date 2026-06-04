#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
int td_cols(void);
int td_rows(void);
bool td_has_color(void);
bool td_has_truecolor(void);
bool td_has_unicode(void);
bool td_is_tmux(void);
bool td_is_screen(void);
bool td_is_ssh(void);
bool td_is_dumb(void);
const char *td_term_type(void);
void td_refresh(void);
int td_format(char *, int);
static int tr = 0, tp = 0;
#define R(f)                                                                                       \
    do {                                                                                           \
        tr++;                                                                                      \
        if (f())                                                                                   \
            tp++;                                                                                  \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int ta(void) {
    assert(td_cols() > 0 && td_rows() > 0);
    return 1;
}
static int tb(void) {
    const char *t = td_term_type();
    assert(t != NULL);
    return 1;
}
static int tc(void) {
    td_refresh();
    assert(td_cols() > 0);
    return 1;
}
static int td(void) {
    char buf[256];
    int n = td_format(buf, 256);
    assert(n > 0 && strstr(buf, "cols="));
    return 1;
}
static int te(void) {
    (void)td_has_color();
    (void)td_has_truecolor();
    (void)td_has_unicode();
    (void)td_is_tmux();
    (void)td_is_screen();
    (void)td_is_ssh();
    (void)td_is_dumb();
    return 1;
}
int test_term_detect_all(void) {
    tr = tp = 0;
    R(ta);
    R(tb);
    R(tc);
    R(td);
    R(te);
    printf("  term_detect: %d/%d passed\n", tp, tr);
    return tr - tp;
}
