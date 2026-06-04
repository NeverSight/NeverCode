#include "test.h"
#include "nevercode.h"
#include "permission.h"
#include <stdlib.h>
#include <string.h>

static int test_exec_rules_bash_deny(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "rm -rf /", EXEC_DENY, "system destruction");

    const char *just = NULL;
    exec_decision_t d = exec_rules_check(&r, "rm -rf /", &just);
    ASSERT_INT_EQ(d, EXEC_DENY);
    ASSERT_STR_EQ(just, "system destruction");

    d = exec_rules_check(&r, "ls -la", &just);
    ASSERT_INT_EQ(d, EXEC_ALLOW);

    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_bash_prompt(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "sudo", EXEC_PROMPT, "requires elevation");

    const char *just = NULL;
    exec_decision_t d = exec_rules_check(&r, "sudo apt install", &just);
    ASSERT_INT_EQ(d, EXEC_PROMPT);
    ASSERT_STR_EQ(just, "requires elevation");

    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_bash_allow(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "git status", EXEC_ALLOW, NULL);
    exec_rules_add(&r, "git push", EXEC_DENY, "no push");

    const char *just = NULL;
    exec_decision_t d;

    d = exec_rules_check(&r, "git status --short", &just);
    ASSERT_INT_EQ(d, EXEC_ALLOW);

    d = exec_rules_check(&r, "git push origin", &just);
    ASSERT_INT_EQ(d, EXEC_DENY);

    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_global_instance(void) {
    exec_rules_ctx_t *g = exec_rules_global();
    ASSERT_NOT_NULL(g);
    exec_rules_init(g);
    exec_rules_add(g, "test-cmd", EXEC_DENY, NULL);
    ASSERT_INT_EQ(g->num_rules, 1);

    const char *j = NULL;
    exec_decision_t d = exec_rules_check(g, "test-cmd foo", &j);
    ASSERT_INT_EQ(d, EXEC_DENY);

    exec_rules_free(g);
    return 0;
}

static int test_review_format_integration(void) {
    review_finding_t findings[2];
    review_finding_init(&findings[0]);
    review_finding_init(&findings[1]);

    review_finding_set(&findings[0], "SQL injection risk",
                       "User input not sanitized in query builder", "src/db.c", 42, 48,
                       REVIEW_CRITICAL);
    review_finding_set(&findings[1], "Memory leak", "malloc without corresponding free",
                       "src/utils.c", 100, 105, REVIEW_ERROR);

    char *summary =
        review_format_summary("Found 2 issues requiring immediate attention.", findings, 2);
    ASSERT_NOT_NULL(summary);
    ASSERT(strstr(summary, "SQL injection") != NULL);
    ASSERT(strstr(summary, "Memory leak") != NULL);
    ASSERT(strstr(summary, "src/db.c:42-48") != NULL);
    ASSERT(strstr(summary, "critical") != NULL);
    ASSERT(strstr(summary, "error") != NULL);
    ASSERT(strstr(summary, "immediate attention") != NULL);

    free(summary);
    review_findings_free(findings, 2);
    return 0;
}

static int test_exec_rules_parse_full_file(void) {
    const char *rules_content = "# Security rules\n"
                                "allow git status\n"
                                "allow git diff\n"
                                "allow git log\n"
                                "deny git push --force # never force push\n"
                                "prompt npm install # may download packages\n"
                                "deny rm -rf # destructive\n"
                                "\n"
                                "# Python rules\n"
                                "prompt python* # script execution\n";

    exec_rules_ctx_t r;
    exec_rules_init(&r);
    int n = exec_rules_parse(&r, rules_content);
    ASSERT_INT_EQ(n, 7);

    const char *just = NULL;
    exec_decision_t d;

    d = exec_rules_check(&r, "git status", &just);
    ASSERT_INT_EQ(d, EXEC_ALLOW);

    d = exec_rules_check(&r, "git push --force origin", &just);
    ASSERT_INT_EQ(d, EXEC_DENY);
    ASSERT_STR_EQ(just, "never force push");

    d = exec_rules_check(&r, "npm install express", &just);
    ASSERT_INT_EQ(d, EXEC_PROMPT);
    ASSERT_STR_EQ(just, "may download packages");

    d = exec_rules_check(&r, "python3 script.py", &just);
    ASSERT_INT_EQ(d, EXEC_PROMPT);

    exec_rules_free(&r);
    return 0;
}

static int test_undo_with_file_history(void) {
    file_history_init();
    file_history_next_turn();
    int turn = file_history_current_turn();
    ASSERT(turn > 0);
    file_history_free();
    return 0;
}

static int test_tab_completion_has_rules(void) {
    extern const char **term_get_slash_cmds(void);
    const char **cmds = term_get_slash_cmds();
    ASSERT_NOT_NULL(cmds);
    bool found_rules = false;
    int count = 0;
    for (int i = 0; cmds[i] != NULL; i++) {
        count++;
        if (strcmp(cmds[i], "/rules") == 0)
            found_rules = true;
    }
    ASSERT(count >= 50);
    ASSERT(found_rules);
    return 0;
}

static int test_exec_rules_with_network_coexist(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "curl", EXEC_PROMPT, "network access");

    network_policy_t np;
    network_policy_init(&np);
    network_policy_add_rule(&np, "*.example.com", NET_ACTION_DENY);

    const char *just = NULL;
    exec_decision_t ed = exec_rules_check(&r, "curl https://api.example.com", &just);
    ASSERT_INT_EQ(ed, EXEC_PROMPT);

    net_action_t na = network_policy_check_url(&np, "https://api.example.com");
    ASSERT_INT_EQ(na, NET_ACTION_DENY);

    exec_rules_free(&r);
    network_policy_free(&np);
    return 0;
}

int test_integration_v3_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("integration_v3");
    RUN_TEST(test_exec_rules_bash_deny);
    RUN_TEST(test_exec_rules_bash_prompt);
    RUN_TEST(test_exec_rules_bash_allow);
    RUN_TEST(test_exec_rules_global_instance);
    RUN_TEST(test_review_format_integration);
    RUN_TEST(test_exec_rules_parse_full_file);
    RUN_TEST(test_undo_with_file_history);
    RUN_TEST(test_tab_completion_has_rules);
    RUN_TEST(test_exec_rules_with_network_coexist);
    TEST_RETURN_FAILED();
}
