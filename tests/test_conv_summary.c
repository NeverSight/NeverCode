#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
int csummary_create(const char *);
int csummary_update(int, int, int, const char *);
int csummary_set_title(int, const char *);
void csummary_close(int);
const char *csummary_title(int);
int csummary_turns(int);
int csummary_tools(int);
bool csummary_is_active(int);
int csummary_count(void);
int csummary_active_count(void);
int csummary_format(int, char *, int);
void csummary_clear(void);
int csummary_find_by_title(const char *);
static int tr_cs = 0, tp_cs = 0;
#define RCS(f)                                                                                     \
    do {                                                                                           \
        tr_cs++;                                                                                   \
        if (f())                                                                                   \
            tp_cs++;                                                                               \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int cs_a(void) {
    csummary_clear();
    int i = csummary_create("Fix bug");
    assert(i == 0 && csummary_count() == 1 && csummary_is_active(i));
    return 1;
}
static int cs_b(void) {
    csummary_clear();
    int i = csummary_create("R");
    csummary_update(i, 5, 12, "D");
    assert(csummary_turns(i) == 5 && csummary_tools(i) == 12);
    return 1;
}
static int cs_c(void) {
    csummary_clear();
    int i = csummary_create("T");
    csummary_close(i);
    assert(!csummary_is_active(i));
    return 1;
}
static int cs_d(void) {
    csummary_clear();
    int i = csummary_create("X");
    csummary_set_title(i, "Custom");
    assert(strcmp(csummary_title(i), "Custom") == 0);
    return 1;
}
static int cs_e(void) {
    csummary_clear();
    int i = csummary_create("F");
    csummary_update(i, 3, 7, NULL);
    char b[512];
    assert(csummary_format(i, b, 512) > 0 && strstr(b, "3 turns"));
    return 1;
}
static int cs_f(void) {
    csummary_clear();
    csummary_create("Alpha");
    csummary_create("Beta");
    assert(csummary_find_by_title("Beta") == 1 && csummary_find_by_title("Delta") == -1);
    return 1;
}
static int cs_g(void) {
    csummary_clear();
    csummary_create("A");
    csummary_create("B");
    csummary_create("C");
    csummary_close(1);
    assert(csummary_active_count() == 2);
    return 1;
}
static int cs_h(void) {
    csummary_clear();
    assert(!csummary_title(-1) && csummary_update(-1, 0, 0, NULL) == -1);
    return 1;
}
static int cs_i(void) {
    csummary_clear();
    csummary_create("X");
    csummary_clear();
    assert(csummary_count() == 0);
    return 1;
}
int test_conv_summary_all(void) {
    tr_cs = tp_cs = 0;
    RCS(cs_a);
    RCS(cs_b);
    RCS(cs_c);
    RCS(cs_d);
    RCS(cs_e);
    RCS(cs_f);
    RCS(cs_g);
    RCS(cs_h);
    RCS(cs_i);
    printf("  conv_summary: %d/%d passed\n", tp_cs, tr_cs);
    return tr_cs - tp_cs;
}
