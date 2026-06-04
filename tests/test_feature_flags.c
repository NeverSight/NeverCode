#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include "defaults.h"
void ff_init(void);
bool ff_enabled(const char *);
void ff_set(const char *, bool);
void ff_reset(const char *);
void ff_reset_all(void);
bool ff_is_known(const char *);
int ff_stage(const char *);
bool ff_default_value(const char *);
int ff_count(void);
const char *ff_key_at(int);
void ff_apply_map(const char *const *, const bool *, int);
int ff_enabled_list(const char **, int);
void ff_normalize_deps(void);
void ff_load_env(void);
static int tr_ff = 0, tp_ff = 0;
#define RFF(f)                                                                                     \
    do {                                                                                           \
        tr_ff++;                                                                                   \
        if (f())                                                                                   \
            tp_ff++;                                                                               \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int ff_a(void) {
    ff_init();
    assert(ff_enabled("shell_tool"));
    assert(!ff_enabled("js_repl"));
    return 1;
}
static int ff_b(void) {
    ff_init();
    ff_set("js_repl", true);
    assert(ff_enabled("js_repl"));
    ff_reset("js_repl");
    assert(!ff_enabled("js_repl"));
    return 1;
}
static int ff_c(void) {
    ff_init();
    ff_set("shell_tool", false);
    ff_reset_all();
    assert(ff_enabled("shell_tool"));
    return 1;
}
static int ff_d(void) {
    ff_init();
    assert(ff_is_known("shell_tool"));
    assert(!ff_is_known("fake"));
    return 1;
}
static int ff_e(void) {
    ff_init();
    assert(ff_stage("shell_tool") == 2);
    assert(ff_stage("js_repl") == 1);
    assert(ff_stage("shell_zsh_fork") == 0);
    return 1;
}
static int ff_f(void) {
    ff_init();
    assert(ff_count() > 0);
    assert(ff_key_at(0));
    return 1;
}
static int ff_g(void) {
    ff_init();
    const char *k[] = {"js_repl", "shell_tool"};
    bool v[] = {true, false};
    ff_apply_map(k, v, 2);
    assert(ff_enabled("js_repl"));
    assert(!ff_enabled("shell_tool"));
    return 1;
}
static int ff_h(void) {
    ff_init();
    const char *l[64];
    assert(ff_enabled_list(l, 64) > 0);
    return 1;
}
static int ff_i(void) {
    ff_init();
    ff_set("code_mode_only", true);
    ff_normalize_deps();
    assert(ff_enabled("code_mode"));
    return 1;
}
static int ff_j(void) {
    ff_init();
    assert(ff_default_value("shell_tool"));
    return 1;
}
static int ff_k(void) {
    ff_init();
    setenv(FF_ENV_VAR, "js_repl,!shell_tool", 1);
    ff_load_env();
    assert(ff_enabled("js_repl"));
    assert(!ff_enabled("shell_tool"));
    unsetenv(FF_ENV_VAR);
    ff_reset_all();
    return 1;
}
int test_feature_flags_all(void) {
    tr_ff = tp_ff = 0;
    RFF(ff_a);
    RFF(ff_b);
    RFF(ff_c);
    RFF(ff_d);
    RFF(ff_e);
    RFF(ff_f);
    RFF(ff_g);
    RFF(ff_h);
    RFF(ff_i);
    RFF(ff_j);
    RFF(ff_k);
    printf("  feature_flags: %d/%d passed\n", tp_ff, tr_ff);
    return tr_ff - tp_ff;
}
