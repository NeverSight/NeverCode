#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
int ansi_strip(const char *, int, char *, int);
bool ansi_has_escapes(const char *, int);
int ansi_visible_len(const char *, int);
int ansi_truncate_visible(const char *, int, int, char *, int);
static int tr = 0, tp = 0;
#define R(f)                                                                                       \
    do {                                                                                           \
        tr++;                                                                                      \
        if (f())                                                                                   \
            tp++;                                                                                  \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int aa(void) {
    const char *s = "plain text";
    char o[64];
    int n = ansi_strip(s, (int)strlen(s), o, 64);
    o[n] = 0;
    assert(strcmp(o, "plain text") == 0);
    return 1;
}
static int ab(void) {
    const char *s = "\033[31mred\033[0m";
    char o[64];
    int n = ansi_strip(s, (int)strlen(s), o, 64);
    o[n] = 0;
    assert(strcmp(o, "red") == 0);
    return 1;
}
static int ac(void) {
    const char *s = "\033[1;32mbold green\033[0m normal";
    char o[128];
    int n = ansi_strip(s, (int)strlen(s), o, 128);
    o[n] = 0;
    assert(strcmp(o, "bold green normal") == 0);
    return 1;
}
static int ad(void) {
    assert(!ansi_has_escapes("hello", 5));
    assert(ansi_has_escapes("\033[m", 3));
    return 1;
}
static int ae(void) {
    assert(ansi_visible_len("plain", 5) == 5);
    assert(ansi_visible_len("\033[31mhi\033[0m", 11) == 2);
    return 1;
}
static int af(void) {
    const char *s = "\033[31mhello world\033[0m";
    char o[64];
    ansi_truncate_visible(s, (int)strlen(s), 5, o, 64);
    assert(strstr(o, "hello") != NULL);
    return 1;
}
static int ag(void) {
    char o[64];
    int n = ansi_strip("", 0, o, 64);
    assert(n == 0);
    return 1;
}
int test_ansi_strip_all(void) {
    tr = tp = 0;
    R(aa);
    R(ab);
    R(ac);
    R(ad);
    R(ae);
    R(af);
    R(ag);
    printf("  ansi_strip: %d/%d passed\n", tp, tr);
    return tr - tp;
}
