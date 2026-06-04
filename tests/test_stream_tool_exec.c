#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static void t1(void) {
    ste_init();
    assert(ste_total_count() == 0);
    ste_free();
    printf("  test_init                                         \033[32mPASS\033[0m\n");
}
static void t2(void) {
    ste_init();
    int i = ste_add_tool("c1", "glob");
    assert(i == 0);
    assert(ste_total_count() == 1);
    assert(ste_pending_count() == 1);
    ste_free();
    printf("  test_add_tool                                     \033[32mPASS\033[0m\n");
}
static void t3(void) {
    ste_init();
    int i = ste_add_tool("c1", "glob");
    assert(ste_can_start(i));
    ste_mark_started(i);
    ste_mark_completed(i, "ok", 0);
    assert(ste_completed_count() == 1);
    assert(!ste_has_error());
    ste_free();
    printf("  test_complete                                     \033[32mPASS\033[0m\n");
}
static void t4(void) {
    ste_init();
    int i = ste_add_tool("c1", "bash");
    int j = ste_add_tool("c2", "grep");
    ste_mark_started(i);
    assert(!ste_can_start(j));
    ste_mark_completed(i, "done", 0);
    assert(ste_can_start(j));
    ste_free();
    printf("  test_exclusive                                    \033[32mPASS\033[0m\n");
}
static void t5(void) {
    ste_init();
    ste_add_tool("c1", "glob");
    ste_add_tool("c2", "grep");
    ste_discard();
    assert(ste_pending_count() == 0);
    ste_free();
    printf("  test_discard                                      \033[32mPASS\033[0m\n");
}
static void t6(void) {
    ste_init();
    ste_add_tool("c1", "bash");
    ste_mark_started(0);
    ste_mark_completed(0, "err", 1);
    assert(ste_has_error());
    char *f = ste_format();
    assert(f && strstr(f, "FAILED"));
    free(f);
    ste_free();
    printf("  test_error                                        \033[32mPASS\033[0m\n");
}
int test_stream_tool_exec_all(void) {
    printf("\n\033[1m[stream_tool_exec]\033[0m\n");
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    printf("test_stream_tool_exec: 6 tests passed\n");
    return 0;
}
