#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_undo_format_list_empty(void) {
    git_undo_init();
    char *out = git_undo_format_list();
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "No") != NULL || strlen(out) > 0);
    free(out);
    git_undo_free();
    return 0;
}

static int test_undo_available_check(void) {
    git_undo_init();
    /* Just verify the function runs; return depends on git repo */
    bool avail = git_undo_available();
    (void)avail;
    git_undo_free();
    return 0;
}

static int test_undo_count_after_init(void) {
    git_undo_init();
    ASSERT_INT_EQ(git_undo_count(), 0);
    git_undo_free();
    return 0;
}

static int test_undo_restore_no_entry(void) {
    git_undo_init();
    int rc = git_undo_restore(999);
    ASSERT(rc != 0);
    git_undo_free();
    return 0;
}

static int test_file_history_undo_integration(void) {
    file_history_init();
    ASSERT_INT_EQ(file_history_current_turn(), 0);
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_current_turn(), 1);
    file_history_free();
    return 0;
}

static int test_exec_rules_undo_check(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "git stash", EXEC_ALLOW, NULL);
    exec_rules_add(&r, "git reset --hard", EXEC_DENY, "destructive");

    const char *just = NULL;
    exec_decision_t d;

    d = exec_rules_check(&r, "git stash push", &just);
    ASSERT_INT_EQ(d, EXEC_ALLOW);

    d = exec_rules_check(&r, "git reset --hard HEAD~1", &just);
    ASSERT_INT_EQ(d, EXEC_DENY);
    ASSERT_STR_EQ(just, "destructive");

    exec_rules_free(&r);
    return 0;
}

int test_undo_enhance_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("undo_enhance");
    RUN_TEST(test_undo_format_list_empty);
    RUN_TEST(test_undo_available_check);
    RUN_TEST(test_undo_count_after_init);
    RUN_TEST(test_undo_restore_no_entry);
    RUN_TEST(test_file_history_undo_integration);
    RUN_TEST(test_exec_rules_undo_check);
    TEST_RETURN_FAILED();
}
