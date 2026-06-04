/*
 * test_git_ops.c — Tests for git operation classification, tracking,
 *                  and fork/exec git helpers.
 */

#include "test.h"
#include "nevercode.h"
#include "git_ops.h"
#include "test_git_helpers.h"

static int test_classify_commit(void) {
    git_op_t op = git_ops_classify("git commit -m 'fix'");
    ASSERT_INT_EQ(op.kind, GIT_OP_COMMIT);
    ASSERT_FALSE(op.destructive);
    return 0;
}

static int test_classify_push(void) {
    git_op_t op = git_ops_classify("git push origin main");
    ASSERT_INT_EQ(op.kind, GIT_OP_PUSH);
    ASSERT_FALSE(op.destructive);
    return 0;
}

static int test_classify_force_push(void) {
    git_op_t op = git_ops_classify("git push --force origin main");
    ASSERT_INT_EQ(op.kind, GIT_OP_PUSH);
    ASSERT_TRUE(op.destructive);
    return 0;
}

static int test_classify_checkout_b(void) {
    git_op_t op = git_ops_classify("git checkout -b feature/x");
    ASSERT_INT_EQ(op.kind, GIT_OP_BRANCH_CREATE);
    return 0;
}

static int test_classify_switch_create(void) {
    git_op_t op = git_ops_classify("git switch -c new-branch");
    ASSERT_INT_EQ(op.kind, GIT_OP_BRANCH_CREATE);
    return 0;
}

static int test_classify_rebase(void) {
    git_op_t op = git_ops_classify("git rebase main");
    ASSERT_INT_EQ(op.kind, GIT_OP_REBASE);
    ASSERT_TRUE(op.destructive);
    return 0;
}

static int test_classify_hard_reset(void) {
    git_op_t op = git_ops_classify("git reset --hard HEAD~3");
    ASSERT_INT_EQ(op.kind, GIT_OP_RESET);
    ASSERT_TRUE(op.destructive);
    return 0;
}

static int test_classify_pr_create(void) {
    git_op_t op = git_ops_classify("gh pr create --title 'fix'");
    ASSERT_INT_EQ(op.kind, GIT_OP_PR_CREATE);
    return 0;
}

static int test_classify_none(void) {
    git_op_t op = git_ops_classify("ls -la");
    ASSERT_INT_EQ(op.kind, GIT_OP_NONE);

    op = git_ops_classify(NULL);
    ASSERT_INT_EQ(op.kind, GIT_OP_NONE);

    op = git_ops_classify("");
    ASSERT_INT_EQ(op.kind, GIT_OP_NONE);
    return 0;
}

static int test_op_name(void) {
    ASSERT_STR_EQ(git_op_name(GIT_OP_COMMIT), "commit");
    ASSERT_STR_EQ(git_op_name(GIT_OP_NONE), "none");
    ASSERT_STR_EQ(git_op_name(GIT_OP_PR_CREATE), "pr-create");
    return 0;
}

static int test_is_mutating(void) {
    ASSERT_TRUE(git_op_is_mutating(GIT_OP_COMMIT));
    ASSERT_TRUE(git_op_is_mutating(GIT_OP_PUSH));
    ASSERT_FALSE(git_op_is_mutating(GIT_OP_STATUS));
    ASSERT_FALSE(git_op_is_mutating(GIT_OP_LOG));
    ASSERT_FALSE(git_op_is_mutating(GIT_OP_DIFF));
    ASSERT_FALSE(git_op_is_mutating(GIT_OP_NONE));
    return 0;
}

static int test_session_tracking(void) {
    git_ops_reset();
    ASSERT_INT_EQ(git_ops_session_count(GIT_OP_COMMIT), 0);

    git_ops_record(GIT_OP_COMMIT, false);
    git_ops_record(GIT_OP_COMMIT, false);
    git_ops_record(GIT_OP_PUSH, true);
    ASSERT_INT_EQ(git_ops_session_count(GIT_OP_COMMIT), 2);
    ASSERT_INT_EQ(git_ops_session_count(GIT_OP_PUSH), 1);
    ASSERT_INT_EQ(git_ops_total_destructive(), 1);

    char *s = git_ops_format_summary();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "commit=2");
    ASSERT_STR_CONTAINS(s, "push=1");
    ASSERT_STR_CONTAINS(s, "destructive");
    free(s);

    git_ops_reset();
    ASSERT_INT_EQ(git_ops_session_count(GIT_OP_COMMIT), 0);
    return 0;
}

static int test_summary_empty(void) {
    git_ops_reset();
    char *s = git_ops_format_summary();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "no git");
    free(s);
    return 0;
}

static int test_chained_commands(void) {
    git_op_t op = git_ops_classify("cd /tmp && git add . && git commit -m 'x'");
    ASSERT_TRUE(op.kind == GIT_OP_ADD || op.kind == GIT_OP_COMMIT);
    return 0;
}

/* ── Fork/exec git_ops helper tests (require a temp repo) ── */

static int test_helper_status_short(void) {
    char tmpdir[] = "/tmp/nc-gitops-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_INT_EQ(test_git_init_repo(tmpdir), 0);

    char buf[GIT_OPS_OUTPUT_BUF];
    ASSERT_INT_EQ(git_status_short(tmpdir, buf, sizeof(buf)), 0);
    ASSERT_INT_EQ(buf[0], '\0');

    char new_file[4096];
    snprintf(new_file, sizeof(new_file), "%s/dirty.txt", tmpdir);
    ASSERT_INT_EQ(test_git_write_file(new_file, "dirty\n"), 0);

    ASSERT_INT_EQ(git_status_short(tmpdir, buf, sizeof(buf)), 0);
    ASSERT_TRUE(buf[0] != '\0');
    ASSERT_STR_CONTAINS(buf, "dirty.txt");

    test_git_cleanup(tmpdir);
    return 0;
}

static int test_helper_current_branch(void) {
    char tmpdir[] = "/tmp/nc-gitops-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_INT_EQ(test_git_init_repo(tmpdir), 0);

    char buf[NC_LABEL_MAX];
    ASSERT_INT_EQ(git_current_branch(tmpdir, buf, sizeof(buf)), 0);
    ASSERT_TRUE(buf[0] != '\0');

    test_git_cleanup(tmpdir);
    return 0;
}

static int test_helper_log_format(void) {
    char tmpdir[] = "/tmp/nc-gitops-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_INT_EQ(test_git_init_repo(tmpdir), 0);

    char buf[NC_LABEL_MAX];
    ASSERT_INT_EQ(git_log_format(tmpdir, 1, "%h %s", buf, sizeof(buf)), 0);
    ASSERT_TRUE(buf[0] != '\0');
    ASSERT_STR_CONTAINS(buf, "base");

    test_git_cleanup(tmpdir);
    return 0;
}

static int test_helper_ls_untracked(void) {
    char tmpdir[] = "/tmp/nc-gitops-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_INT_EQ(test_git_init_repo(tmpdir), 0);

    char buf[GIT_OPS_OUTPUT_BUF];
    ASSERT_INT_EQ(git_ls_untracked(tmpdir, buf, sizeof(buf)), 0);
    ASSERT_INT_EQ(buf[0], '\0');

    char new_file[4096];
    snprintf(new_file, sizeof(new_file), "%s/untracked.txt", tmpdir);
    ASSERT_INT_EQ(test_git_write_file(new_file, "new\n"), 0);

    ASSERT_INT_EQ(git_ls_untracked(tmpdir, buf, sizeof(buf)), 0);
    ASSERT_STR_CONTAINS(buf, "untracked.txt");

    test_git_cleanup(tmpdir);
    return 0;
}

static int test_helper_diff_ex(void) {
    char tmpdir[] = "/tmp/nc-gitops-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_INT_EQ(test_git_init_repo(tmpdir), 0);

    char note_path[4096];
    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    ASSERT_INT_EQ(test_git_write_file(note_path, "modified\n"), 0);

    char buf[GIT_OPS_OUTPUT_BUF];
    ASSERT_INT_EQ(git_diff_ex(tmpdir, false, NULL, buf, sizeof(buf)), 0);
    ASSERT_STR_CONTAINS(buf, "modified");

    ASSERT_INT_EQ(git_diff_stat_ex(tmpdir, false, NULL, buf, sizeof(buf)), 0);
    ASSERT_STR_CONTAINS(buf, "note.txt");

    ASSERT_INT_EQ(git_diff_ex(tmpdir, true, NULL, buf, sizeof(buf)), 0);
    ASSERT_INT_EQ(buf[0], '\0');

    test_git_cleanup(tmpdir);
    return 0;
}

static int test_helper_cmd_alloc(void) {
    char tmpdir[] = "/tmp/nc-gitops-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_INT_EQ(test_git_init_repo(tmpdir), 0);

    const char *args[] = {"log", "--oneline", "-1"};
    char *out = git_cmd_alloc(tmpdir, args, 3, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_TRUE(strlen(out) > 0);
    ASSERT_STR_CONTAINS(out, "base");
    free(out);

    const char *bad_args[] = {"log", "--oneline", "--invalid-option-XXXXXX"};
    char *bad = git_cmd_alloc(tmpdir, bad_args, 3, 0);
    ASSERT_NULL(bad);

    test_git_cleanup(tmpdir);
    return 0;
}

static int test_helper_non_repo(void) {
    char tmpdir[] = "/tmp/nc-gitops-nonrepo-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));

    ASSERT_FALSE(git_is_repo(tmpdir));

    char buf[NC_LABEL_MAX];
    ASSERT_TRUE(git_current_branch(tmpdir, buf, sizeof(buf)) != 0);
    ASSERT_TRUE(git_status_short(tmpdir, buf, sizeof(buf)) != 0);

    test_git_cleanup(tmpdir);
    return 0;
}

int test_git_ops_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("git_ops");
    RUN_TEST(test_classify_commit);
    RUN_TEST(test_classify_push);
    RUN_TEST(test_classify_force_push);
    RUN_TEST(test_classify_checkout_b);
    RUN_TEST(test_classify_switch_create);
    RUN_TEST(test_classify_rebase);
    RUN_TEST(test_classify_hard_reset);
    RUN_TEST(test_classify_pr_create);
    RUN_TEST(test_classify_none);
    RUN_TEST(test_op_name);
    RUN_TEST(test_is_mutating);
    RUN_TEST(test_session_tracking);
    RUN_TEST(test_summary_empty);
    RUN_TEST(test_chained_commands);
    RUN_TEST(test_helper_status_short);
    RUN_TEST(test_helper_current_branch);
    RUN_TEST(test_helper_log_format);
    RUN_TEST(test_helper_ls_untracked);
    RUN_TEST(test_helper_diff_ex);
    RUN_TEST(test_helper_cmd_alloc);
    RUN_TEST(test_helper_non_repo);
    return _t_failed;
}
