#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
typedef int (*dyn_tool_fn)(const char *, char *, int);
int dyntool_register(const char *, const char *, const char *, dyn_tool_fn);
int dyntool_unregister(const char *);
int dyntool_invoke(const char *, const char *, char *, int);
bool dyntool_exists(const char *);
int dyntool_count(void);
int dyntool_list(const char **, int);
const char *dyntool_description(const char *);
const char *dyntool_source(const char *);
int dyntool_param_count(const char *);
void dyntool_clear_all(void);
int dyntool_add_param(const char *, const char *, const char *, bool);
int dyntool_list_by_source(const char *, const char **, int);
static int dt_eh(const char *i, char *o, int c) {
    int l = (int)strlen(i);
    if (l >= c)
        l = c - 1;
    memcpy(o, i, l);
    o[l] = 0;
    return 0;
}
static int dt_uh(const char *i, char *o, int c) {
    int x;
    for (x = 0; i[x] && x < c - 1; x++)
        o[x] = (i[x] >= 'a' && i[x] <= 'z') ? i[x] - 32 : i[x];
    o[x] = 0;
    return 0;
}
static int tr_dt = 0, tp_dt = 0;
#define RDT(f)                                                                                     \
    do {                                                                                           \
        tr_dt++;                                                                                   \
        if (f())                                                                                   \
            tp_dt++;                                                                               \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int dt_a(void) {
    dyntool_clear_all();
    assert(dyntool_register("echo", "E", "t", dt_eh) == 0);
    char o[64];
    assert(dyntool_invoke("echo", "hi", o, 64) == 0 && strcmp(o, "hi") == 0);
    return 1;
}
static int dt_b(void) {
    dyntool_clear_all();
    dyntool_register("tmp", "T", "t", dt_eh);
    dyntool_unregister("tmp");
    assert(!dyntool_exists("tmp"));
    return 1;
}
static int dt_c(void) {
    dyntool_clear_all();
    assert(dyntool_register("d", "A", "t", dt_eh) == 0 &&
           dyntool_register("d", "B", "t", dt_eh) == -2);
    return 1;
}
static int dt_d(void) {
    dyntool_clear_all();
    dyntool_register("a", "A", "s", dt_eh);
    dyntool_register("b", "B", "s", dt_uh);
    assert(dyntool_count() == 2);
    return 1;
}
static int dt_e(void) {
    dyntool_clear_all();
    dyntool_register("tt", "Desc", "plug", dt_eh);
    assert(strcmp(dyntool_description("tt"), "Desc") == 0);
    return 1;
}
static int dt_f(void) {
    dyntool_clear_all();
    dyntool_register("pt", "P", "t", dt_eh);
    dyntool_add_param("pt", "in", "input", true);
    assert(dyntool_param_count("pt") == 1);
    return 1;
}
static int dt_g(void) {
    dyntool_clear_all();
    char o[64];
    assert(dyntool_invoke("nope", "x", o, 64) == -1);
    return 1;
}
static int dt_h(void) {
    dyntool_clear_all();
    dyntool_register("m1", "M", "mcp", dt_eh);
    dyntool_register("l1", "L", "local", dt_eh);
    dyntool_register("m2", "M", "mcp", dt_uh);
    const char *n[8];
    assert(dyntool_list_by_source("mcp", n, 8) == 2);
    return 1;
}
static int dt_i(void) {
    dyntool_clear_all();
    assert(dyntool_register(NULL, "d", "s", dt_eh) == -1 &&
           dyntool_register("f", "d", "s", NULL) == -1);
    return 1;
}
int test_dynamic_tool_all(void) {
    tr_dt = tp_dt = 0;
    RDT(dt_a);
    RDT(dt_b);
    RDT(dt_c);
    RDT(dt_d);
    RDT(dt_e);
    RDT(dt_f);
    RDT(dt_g);
    RDT(dt_h);
    RDT(dt_i);
    printf("  dynamic_tool: %d/%d passed\n", tp_dt, tr_dt);
    return tr_dt - tp_dt;
}
