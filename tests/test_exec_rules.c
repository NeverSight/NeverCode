#include "test.h"
#include "nevercode.h"
#include "defaults.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

static int test_exec_rules_init(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    ASSERT_INT_EQ(r.num_rules, 0);
    ASSERT_INT_EQ(r.default_decision, EXEC_ALLOW);
    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_add(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    int rc = exec_rules_add(&r, "git push", EXEC_DENY, "too dangerous");
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(r.num_rules, 1);
    ASSERT_STR_EQ(r.rules[0].prefix, "git push");
    ASSERT_INT_EQ(r.rules[0].decision, EXEC_DENY);
    ASSERT_STR_EQ(r.rules[0].justification, "too dangerous");
    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_parse_allow(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    int n = exec_rules_parse(&r, "allow git status\nallow ls\n");
    ASSERT_INT_EQ(n, 2);
    ASSERT_INT_EQ(r.num_rules, 2);
    ASSERT_STR_EQ(r.rules[0].prefix, "git status");
    ASSERT_INT_EQ(r.rules[0].decision, EXEC_ALLOW);
    ASSERT_STR_EQ(r.rules[1].prefix, "ls");
    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_parse_deny_justification(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    int n = exec_rules_parse(&r, "deny rm -rf # extremely dangerous\n");
    ASSERT_INT_EQ(n, 1);
    ASSERT_STR_EQ(r.rules[0].prefix, "rm -rf");
    ASSERT_INT_EQ(r.rules[0].decision, EXEC_DENY);
    ASSERT_STR_EQ(r.rules[0].justification, "extremely dangerous");
    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_parse_comments(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    int n = exec_rules_parse(&r, "# this is a comment\nallow cat\n  # another\n");
    ASSERT_INT_EQ(n, 1);
    ASSERT_STR_EQ(r.rules[0].prefix, "cat");
    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_parse_prompt(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    int n = exec_rules_parse(&r, "prompt curl # network access\n");
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(r.rules[0].decision, EXEC_PROMPT);
    ASSERT_STR_EQ(r.rules[0].justification, "network access");
    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_check_prefix(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "git", EXEC_ALLOW, NULL);
    exec_rules_add(&r, "git push", EXEC_DENY, "no push");

    const char *just = NULL;
    exec_decision_t d;

    d = exec_rules_check(&r, "git status", &just);
    ASSERT_INT_EQ(d, EXEC_ALLOW);

    d = exec_rules_check(&r, "git push origin main", &just);
    ASSERT_INT_EQ(d, EXEC_DENY);
    ASSERT_STR_EQ(just, "no push");

    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_check_longest_match(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "npm", EXEC_PROMPT, NULL);
    exec_rules_add(&r, "npm test", EXEC_ALLOW, NULL);
    exec_rules_add(&r, "npm run", EXEC_PROMPT, "arbitrary script");

    const char *just = NULL;
    exec_decision_t d;

    d = exec_rules_check(&r, "npm test", &just);
    ASSERT_INT_EQ(d, EXEC_ALLOW);

    d = exec_rules_check(&r, "npm run build", &just);
    ASSERT_INT_EQ(d, EXEC_PROMPT);

    d = exec_rules_check(&r, "npm install", &just);
    ASSERT_INT_EQ(d, EXEC_PROMPT);

    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_check_no_match(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "git", EXEC_DENY, NULL);

    const char *just = NULL;
    exec_decision_t d = exec_rules_check(&r, "ls -la", &just);
    ASSERT_INT_EQ(d, EXEC_ALLOW);
    ASSERT_NULL(just);

    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_check_wildcard(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "python*", EXEC_PROMPT, "script execution");

    const char *just = NULL;
    exec_decision_t d;

    d = exec_rules_check(&r, "python3 script.py", &just);
    ASSERT_INT_EQ(d, EXEC_PROMPT);
    ASSERT_STR_EQ(just, "script execution");

    d = exec_rules_check(&r, "ruby script.rb", &just);
    ASSERT_INT_EQ(d, EXEC_ALLOW);

    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_format(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "git push", EXEC_DENY, "dangerous");
    exec_rules_add(&r, "ls", EXEC_ALLOW, NULL);

    char *out = exec_rules_format(&r);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "deny") != NULL);
    ASSERT(strstr(out, "git push") != NULL);
    ASSERT(strstr(out, "dangerous") != NULL);
    ASSERT(strstr(out, "allow") != NULL);
    free(out);

    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_format_empty(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    char *out = exec_rules_format(&r);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "No exec rules") != NULL);
    free(out);
    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_null_args(void) {
    ASSERT_INT_EQ(exec_rules_add(NULL, "x", EXEC_ALLOW, NULL), -1);
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    ASSERT_INT_EQ(exec_rules_add(&r, NULL, EXEC_ALLOW, NULL), -1);
    ASSERT_INT_EQ(exec_rules_check(NULL, "x", NULL), EXEC_ALLOW);
    ASSERT_INT_EQ(exec_rules_check(&r, NULL, NULL), EXEC_ALLOW);
    exec_rules_free(&r);
    return 0;
}

static int test_exec_rules_persist_allow(void) {
    char tmpdir[128];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_ertest_%d", (int)getpid());
    mkdir(tmpdir, NC_DIR_MODE);

    char config_dir[NC_PATH_MAX];
    snprintf(config_dir, sizeof(config_dir), "%s" CONFIG_DIR_PRIMARY, tmpdir);
    mkdir(config_dir, NC_DIR_MODE);

    char old_home[NC_PATH_MAX];
    const char *orig = nc_home_dir();
    if (orig)
        nc_strlcpy(old_home, orig, sizeof(old_home));

    setenv("HOME", tmpdir, 1);
    nc_home_dir_reset();
    config_home_reset_cache();

    exec_rules_ctx_t r;
    exec_rules_init(&r);
    int rc = exec_rules_persist_allow(&r, "npm test", "safe command");
    ASSERT_INT_EQ(rc, 0);

    char rule_path[NC_PATH_MAX];
    config_home_path(rule_path, sizeof(rule_path), "rules");
    char fpath[NC_PATH_MAX];
    snprintf(fpath, sizeof(fpath), "%s/default.rules", rule_path);

    size_t sz = 0;
    char *content = strbuf_read_file(fpath, &sz);
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "allow npm test");
    ASSERT_STR_CONTAINS(content, "safe command");
    free(content);

    exec_rules_free(&r);

    if (orig)
        setenv("HOME", old_home, 1);
    nc_home_dir_reset();
    config_home_reset_cache();

    nc_rm_rf(tmpdir);
    return 0;
}

int test_exec_rules_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("exec_rules");
    RUN_TEST(test_exec_rules_init);
    RUN_TEST(test_exec_rules_add);
    RUN_TEST(test_exec_rules_parse_allow);
    RUN_TEST(test_exec_rules_parse_deny_justification);
    RUN_TEST(test_exec_rules_parse_comments);
    RUN_TEST(test_exec_rules_parse_prompt);
    RUN_TEST(test_exec_rules_check_prefix);
    RUN_TEST(test_exec_rules_check_longest_match);
    RUN_TEST(test_exec_rules_check_no_match);
    RUN_TEST(test_exec_rules_check_wildcard);
    RUN_TEST(test_exec_rules_format);
    RUN_TEST(test_exec_rules_format_empty);
    RUN_TEST(test_exec_rules_null_args);
    RUN_TEST(test_exec_rules_persist_allow);
    TEST_RETURN_FAILED();
}
