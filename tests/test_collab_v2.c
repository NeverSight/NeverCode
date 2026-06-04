#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static void t1(void) {
    collab_v2_init();
    assert(collab_v2_active_count() == 0);
    collab_v2_free();
    printf("  test_init                                         \033[32mPASS\033[0m\n");
}
static void t2(void) {
    collab_v2_init();
    char tid[64];
    assert(collab_v2_spawn("w", "task", 0, 0, tid, sizeof(tid)) == 0);
    assert(collab_v2_active_count() == 1);
    collab_v2_complete(tid, "done");
    assert(collab_v2_active_count() == 0);
    collab_v2_free();
    printf("  test_spawn_complete                                \033[32mPASS\033[0m\n");
}
static void t3(void) {
    collab_v2_init();
    char *j = collab_v2_list_json();
    assert(j);
    free(j);
    collab_v2_free();
    printf("  test_list                                         \033[32mPASS\033[0m\n");
}
int test_collab_v2_all(void) {
    printf("\n\033[1m[collab_v2]\033[0m\n");
    t1();
    t2();
    t3();
    printf("test_collab_v2: 3 tests passed\n");
    return 0;
}
