#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static void t1(void) {
    models_mgr_init();
    assert(models_mgr_count() == 0);
    assert(models_mgr_stale());
    models_mgr_free();
    printf("  test_init                                         \033[32mPASS\033[0m\n");
}
static void t2(void) {
    models_mgr_init();
    assert(models_mgr_find("nonexistent") == -1);
    assert(models_mgr_context_window(-1) == 0);
    models_mgr_free();
    printf("  test_find_missing                                 \033[32mPASS\033[0m\n");
}
static void t3(void) {
    models_mgr_init();
    char *j = models_mgr_list_json();
    assert(j != NULL);
    free(j);
    models_mgr_free();
    printf("  test_list_empty                                   \033[32mPASS\033[0m\n");
}
int test_models_mgr_all(void) {
    printf("\n\033[1m[models_mgr]\033[0m\n");
    t1();
    t2();
    t3();
    printf("test_models_mgr: 3 tests passed\n");
    return 0;
}
