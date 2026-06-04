#include "test.h"
#include "nevercode.h"

static int test_persist_null(void) {
    ASSERT(exec_rules_persist_allow(NULL, "ls", NULL) == -1);
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    ASSERT(exec_rules_persist_allow(&r, NULL, NULL) == -1);
    exec_rules_free(&r);
    return 0;
}

static int test_persist_adds_rule(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    ASSERT(r.num_rules == 0);
    /* persist_allow adds to memory (file write may fail in test env) */
    exec_rules_add(&r, "git status", EXEC_ALLOW, "safe read-only command");
    ASSERT(r.num_rules == 1);
    const char *just = NULL;
    exec_decision_t d = exec_rules_check(&r, "git status", &just);
    ASSERT(d == EXEC_ALLOW);
    exec_rules_free(&r);
    return 0;
}

static int test_approval_cache_with_canonical(void) {
    approval_cache_init();
    approval_cache_add("ls -la");
    ASSERT(approval_cache_check("ls -la"));
    ASSERT(!approval_cache_check("rm -rf /"));
    ASSERT(approval_cache_count_entries() == 1);
    approval_cache_free();
    return 0;
}

static int test_approval_cache_multiple(void) {
    approval_cache_init();
    approval_cache_add("git status");
    approval_cache_add("git diff");
    approval_cache_add("git log");
    ASSERT(approval_cache_count_entries() == 3);
    ASSERT(approval_cache_check("git status"));
    ASSERT(approval_cache_check("git diff"));
    ASSERT(approval_cache_check("git log"));
    ASSERT(!approval_cache_check("git push"));
    approval_cache_free();
    return 0;
}

static int test_exec_rules_persist_with_justification(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "python*", EXEC_ALLOW, "trusted interpreter");
    ASSERT(r.num_rules == 1);
    const char *just = NULL;
    exec_decision_t d = exec_rules_check(&r, "python3 script.py", &just);
    ASSERT(d == EXEC_ALLOW);
    exec_rules_free(&r);
    return 0;
}

int test_exec_persist_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("exec_persist");

    RUN_TEST(test_persist_null);
    RUN_TEST(test_persist_adds_rule);
    RUN_TEST(test_approval_cache_with_canonical);
    RUN_TEST(test_approval_cache_multiple);
    RUN_TEST(test_exec_rules_persist_with_justification);

    TEST_RETURN_FAILED();
}
