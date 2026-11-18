#include "test.h"
#include "nevercode.h"
#include "permission.h"

static int test_perm_init(void) {
    perm_ctx_t p;
    perm_init(&p);
    ASSERT_INT_EQ(p.num_rules, 0);
    ASSERT_INT_EQ(p.trust_all, 0);
    ASSERT_INT_EQ(p.approval_preset, APPROVAL_DEFAULT);
    perm_free(&p);
    return 0;
}

static int test_perm_trust_all(void) {
    perm_ctx_t p;
    perm_init(&p);
    p.trust_all = true; /* explicit opt-in, e.g. --dangerously-skip-permissions */
    ASSERT_INT_EQ(perm_check(&p, "Bash", NULL), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Write", "/etc/passwd"), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Edit", "/tmp/test"), PERM_ALLOW);
    perm_free(&p);
    return 0;
}

static int test_perm_safe_tools(void) {
    perm_ctx_t p;
    perm_init(&p);
    ASSERT_INT_EQ(perm_check(&p, "Read", NULL), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Glob", NULL), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Grep", NULL), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "TodoWrite", NULL), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "WebFetch", NULL), PERM_ASK);
    ASSERT_INT_EQ(perm_check(&p, "WebSearch", NULL), PERM_ASK);
    perm_free(&p);
    return 0;
}

static int test_perm_standard_workspace(void) {
    perm_ctx_t p;
    perm_init(&p);
    ASSERT_INT_EQ(perm_check(&p, "Bash", NULL), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Write", "/tmp/test"), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Edit", "/tmp/test"), PERM_ALLOW);
    /* Relative path inside the workspace (tests run with repo as CWD) */
    ASSERT_INT_EQ(perm_check(&p, "Write", "tests/test_permission.c"), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Edit", "src/../tests/test_permission.c"), PERM_ALLOW);
    /* Outside the workspace: approval required */
    ASSERT_INT_EQ(perm_check(&p, "Write", "/etc/passwd"), PERM_ASK);
    ASSERT_INT_EQ(perm_check(&p, "Edit", "/etc/cron.d/x"), PERM_ASK);
    ASSERT_INT_EQ(perm_check(&p, "Write", "/tmp/../../etc/passwd"), PERM_ASK);
    perm_free(&p);
    return 0;
}

static int test_perm_standard_network(void) {
    perm_ctx_t p;
    perm_init(&p);
    /* Network tools require approval under "standard" even though read-only */
    ASSERT_INT_EQ(perm_check(&p, "WebFetch", NULL), PERM_ASK);
    ASSERT_INT_EQ(perm_check(&p, "WebSearch", NULL), PERM_ASK);
    perm_free(&p);
    return 0;
}

static int test_perm_full_access(void) {
    perm_ctx_t p;
    perm_init(&p);
    perm_set_approval(&p, APPROVAL_FULL);
    ASSERT_INT_EQ(p.trust_all, 1);
    ASSERT_INT_EQ(perm_check(&p, "WebFetch", NULL), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Write", "/etc/passwd"), PERM_ALLOW);
    perm_free(&p);
    return 0;
}

static int test_perm_readonly_mode(void) {
    perm_ctx_t p;
    perm_init(&p);
    perm_set_approval(&p, APPROVAL_READONLY);
    ASSERT_INT_EQ(p.trust_all, 0);
    perm_free(&p);
    return 0;
}

static int test_perm_add_rule(void) {
    perm_ctx_t p;
    perm_init(&p);
    p.trust_all = false;
    perm_add_rule(&p, "Bash", NULL, PERM_ALLOW);
    ASSERT_INT_EQ(p.num_rules, 1);
    ASSERT_INT_EQ(perm_check(&p, "Bash", NULL), PERM_ALLOW);
    perm_free(&p);
    return 0;
}

static int test_perm_path_prefix(void) {
    perm_ctx_t p;
    perm_init(&p);
    p.trust_all = false;
    perm_add_rule(&p, "Write", "/home/user/project/", PERM_ALLOW);

    ASSERT_INT_EQ(perm_check(&p, "Write", "/home/user/project/file.c"), PERM_ALLOW);
    perm_free(&p);
    return 0;
}

static int test_perm_wildcard_rule(void) {
    perm_ctx_t p;
    perm_init(&p);
    p.trust_all = false;
    perm_add_rule(&p, "*", NULL, PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Bash", NULL), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Write", NULL), PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Agent", NULL), PERM_ALLOW);
    perm_free(&p);
    return 0;
}

static int test_perm_deny_rule(void) {
    perm_ctx_t p;
    perm_init(&p);
    p.trust_all = false;
    perm_add_rule(&p, "Bash", NULL, PERM_DENY);
    ASSERT_INT_EQ(perm_check(&p, "Bash", NULL), PERM_DENY);
    perm_free(&p);
    return 0;
}

static int test_perm_last_rule_wins(void) {
    perm_ctx_t p;
    perm_init(&p);
    p.trust_all = false;
    perm_add_rule(&p, "Bash", NULL, PERM_DENY);
    perm_add_rule(&p, "Bash", NULL, PERM_ALLOW);
    ASSERT_INT_EQ(perm_check(&p, "Bash", NULL), PERM_ALLOW);
    perm_free(&p);
    return 0;
}

int test_permission_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("permission");
    RUN_TEST(test_perm_init);
    RUN_TEST(test_perm_trust_all);
    RUN_TEST(test_perm_safe_tools);
    RUN_TEST(test_perm_standard_workspace);
    RUN_TEST(test_perm_standard_network);
    RUN_TEST(test_perm_full_access);
    RUN_TEST(test_perm_readonly_mode);
    RUN_TEST(test_perm_add_rule);
    RUN_TEST(test_perm_path_prefix);
    RUN_TEST(test_perm_wildcard_rule);
    RUN_TEST(test_perm_deny_rule);
    RUN_TEST(test_perm_last_rule_wins);
    TEST_RETURN_FAILED();
}
