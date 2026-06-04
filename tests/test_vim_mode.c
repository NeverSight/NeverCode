#include "nevercode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int tr = 0;
static void t1(void) {
    vim_mode_init();
    assert(!vim_mode_enabled());
    vim_mode_set_enabled(true);
    assert(vim_mode_enabled());
    vim_mode_free();
    tr++;
}
static void t2(void) {
    vim_mode_init();
    vim_mode_set_enabled(true);
    const char *b = "hello world";
    int l = (int)strlen(b);
    assert(vim_motion(b, l, 0, 'l', 1) == 1);
    assert(vim_motion(b, l, 5, 'h', 2) == 3);
    vim_mode_free();
    tr++;
}
static void t3(void) {
    vim_mode_init();
    vim_mode_set_enabled(true);
    const char *b = "foo bar";
    assert(vim_motion(b, (int)strlen(b), 0, 'w', 1) == 4);
    vim_mode_free();
    tr++;
}
static void t4(void) {
    vim_mode_init();
    vim_mode_set_enabled(true);
    char b[64];
    nc_strlcpy(b, "hello world", sizeof(b));
    int l = (int)strlen(b);
    int p = vim_operator(b, &l, 0, 'd', 0, 5);
    assert(p == 0 && strcmp(b, " world") == 0);
    vim_mode_free();
    tr++;
}
static void t5(void) {
    vim_mode_init();
    vim_mode_set_enabled(true);
    char b[64];
    nc_strlcpy(b, "abc", sizeof(b));
    int l = 3;
    vim_operator(b, &l, 0, 'y', 0, 3);
    assert(strncmp(vim_register_content('"'), "abc", 3) == 0);
    vim_mode_free();
    tr++;
}
static void t6(void) {
    vim_mode_init();
    vim_mode_set_enabled(true);
    int s, e;
    assert(vim_text_object("hello", (int)strlen("hello"), 2, 'w', true, &s, &e) == 0);
    assert(s == 0 && e == 5);
    vim_mode_free();
    tr++;
}
static void t7(void) {
    vim_mode_init();
    vim_mode_set_enabled(true);
    char *f = vim_mode_format();
    assert(f && strstr(f, "Vim:"));
    free(f);
    vim_mode_free();
    tr++;
}
int test_vim_mode_all(void) {
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    t7();
    printf("test_vim_mode: %d tests passed\n", tr);
    return 0;
}
