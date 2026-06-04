#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static void t1(void) {
    assert(usc_total_commands() == 0);
    printf("  test_stats_init                                   \033[32mPASS\033[0m\n");
}
static void t2(void) {
    char *s = usc_format_stats();
    assert(s && strstr(s, "UserShell"));
    free(s);
    printf("  test_format                                       \033[32mPASS\033[0m\n");
}
int test_user_shell_cmd_all(void) {
    printf("\n\033[1m[user_shell_cmd]\033[0m\n");
    t1();
    t2();
    printf("test_user_shell_cmd: 2 tests passed\n");
    return 0;
}
