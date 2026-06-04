#include "nevercode.h"
#include "conversation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static void t1(void) {
    assert(count_api_rounds(NULL, 0) == 0);
    printf("  test_empty                                        \033[32mPASS\033[0m\n");
}
static void t2(void) {
    conversation_t c;
    conversation_init(&c);
    conversation_add_user_text(&c, "Hi");
    message_t *a = conversation_add(&c, ROLE_ASSISTANT);
    message_add_text(a, "Hello");
    int n = count_api_rounds(c.msgs, c.len);
    assert(n >= 1);
    conversation_free(&c);
    printf("  test_single_round                                 \033[32mPASS\033[0m\n");
}
int test_round_grouping_all(void) {
    printf("\n\033[1m[round_grouping]\033[0m\n");
    t1();
    t2();
    printf("test_round_grouping: 2 tests passed\n");
    return 0;
}
