#include "nevercode.h"
#include "conversation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static void t1(void) {
    session_snapshot_free_all();
    conversation_t c;
    conversation_init(&c);
    conversation_add_user_text(&c, "Hi");
    message_t *a = conversation_add(&c, ROLE_ASSISTANT);
    message_add_text(a, "Hey");
    assert(session_snapshot_save("s1", &c, "gpt-5.4", 100, 50, 10, NULL) == 0);
    assert(session_snapshot_count() == 1);
    conversation_free(&c);
    printf("  test_save                                         \033[32mPASS\033[0m\n");
}
static void t2(void) {
    conversation_t c;
    conversation_init(&c);
    assert(session_snapshot_load("s1", &c) == 2);
    conversation_free(&c);
    session_snapshot_free_all();
    printf("  test_load                                         \033[32mPASS\033[0m\n");
}
int test_session_snapshot_all(void) {
    printf("\n\033[1m[session_snapshot]\033[0m\n");
    t1();
    t2();
    printf("test_session_snapshot: 2 tests passed\n");
    return 0;
}
