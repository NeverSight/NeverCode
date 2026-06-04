#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static void t1(void) {
    hook_ctx_t h;
    hooks_init(&h);
    char *c = NULL;
    hook_decision_t d = hook_runtime_execute(&h, HOOK_PRE_TOOL_USE, "bash", "{}", &c);
    assert(d == HOOK_DECISION_PROCEED);
    assert(!c);
    hooks_free(&h);
    printf("  test_no_hooks                                     \033[32mPASS\033[0m\n");
}
static void t2(void) {
    hook_runtime_reset_stats();
    assert(hook_runtime_total_runs() == 0);
    printf("  test_stats_reset                                  \033[32mPASS\033[0m\n");
}
static void t3(void) {
    char *s = hook_runtime_format_stats();
    assert(s);
    free(s);
    printf("  test_format                                       \033[32mPASS\033[0m\n");
}
static void t4(void) {
    hook_ctx_t h;
    hooks_init(&h);
    char *c = NULL;
    hook_decision_t d = hook_runtime_execute(&h, HOOK_TEAMMATE_IDLE, NULL, NULL, &c);
    assert(d == HOOK_DECISION_PROCEED);
    assert(!c);
    d = hook_runtime_execute(&h, HOOK_TASK_COMPLETED, NULL, NULL, &c);
    assert(d == HOOK_DECISION_PROCEED);
    assert(!c);
    hooks_free(&h);
    printf("  test_new_hook_events                              \033[32mPASS\033[0m\n");
}
int test_hook_runtime_all(void) {
    printf("\n\033[1m[hook_runtime]\033[0m\n");
    t1();
    t2();
    t3();
    t4();
    printf("test_hook_runtime: 4 tests passed\n");
    return 0;
}
