#include "nevercode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int tr = 0;
static void t1(void) {
    output_style_init();
    assert(output_style_color());
    output_style_free();
    tr++;
}
static void t2(void) {
    output_style_init();
    output_style_set_color(false);
    assert(!output_style_color());
    output_style_free();
    tr++;
}
static void t3(void) {
    output_style_init();
    char *h = output_style_header("T");
    assert(h && strlen(h) > 0);
    free(h);
    output_style_free();
    tr++;
}
static void t4(void) {
    output_style_init();
    char *c = output_style_tool_call("bash", "ls");
    assert(c && strstr(c, "bash"));
    free(c);
    output_style_free();
    tr++;
}
static void t5(void) {
    output_style_init();
    char *t = output_style_text("Hi");
    assert(t && strstr(t, "Hi"));
    free(t);
    output_style_free();
    tr++;
}
static void t6(void) {
    output_style_init();
    char *f = output_style_footer(10, 20, 0.01);
    assert(f);
    free(f);
    output_style_free();
    tr++;
}
static void t7(void) {
    output_style_init();
    char *f = output_style_format();
    assert(f && strstr(f, "Output Style"));
    free(f);
    output_style_free();
    tr++;
}
int test_output_style_all(void) {
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    t7();
    printf("test_output_style: %d tests passed\n", tr);
    return 0;
}
