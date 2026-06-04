#include "test.h"
#include "test_helpers.h"
#include "nevercode.h"
#include "conversation.h"
#include "git_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_init_free(void) {
    conv_branch_init();
    ASSERT_INT_EQ(conv_branch_count(), 0);
    conv_branch_free();
    return 0;
}

static int test_create_basic(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-XXXXXX";
    ASSERT_INT_EQ(th_enter_temp_dir(saved_cwd, sizeof(saved_cwd), tmpdir), 0);
    conv_branch_init();
    int rc = conv_branch_create("session-abc", "my-branch", 10);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(conv_branch_count(), 1);
    ASSERT_STR_EQ(conv_branch_session_id(0), "session-abc");
    ASSERT_STR_EQ(conv_branch_name(0), "my-branch");
    ASSERT_INT_EQ(conv_branch_msg_count(0), 10);
    conv_branch_free();
    ASSERT_INT_EQ(th_leave_temp_dir(saved_cwd, tmpdir), 0);
    return 0;
}

static int test_create_unnamed(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-XXXXXX";
    ASSERT_INT_EQ(th_enter_temp_dir(saved_cwd, sizeof(saved_cwd), tmpdir), 0);
    conv_branch_init();
    int rc = conv_branch_create("session-xyz", NULL, 5);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NULL(conv_branch_name(0));
    ASSERT_INT_EQ(conv_branch_msg_count(0), 5);
    conv_branch_free();
    ASSERT_INT_EQ(th_leave_temp_dir(saved_cwd, tmpdir), 0);
    return 0;
}

static int test_create_multiple(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-XXXXXX";
    ASSERT_INT_EQ(th_enter_temp_dir(saved_cwd, sizeof(saved_cwd), tmpdir), 0);
    conv_branch_init();
    conv_branch_create("s1", "branch-1", 3);
    conv_branch_create("s2", "branch-2", 7);
    conv_branch_create("s3", NULL, 12);
    ASSERT_INT_EQ(conv_branch_count(), 3);
    ASSERT_STR_EQ(conv_branch_session_id(1), "s2");
    ASSERT_STR_EQ(conv_branch_name(1), "branch-2");
    ASSERT_INT_EQ(conv_branch_msg_count(2), 12);
    conv_branch_free();
    ASSERT_INT_EQ(th_leave_temp_dir(saved_cwd, tmpdir), 0);
    return 0;
}

static int test_find_by_name(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-XXXXXX";
    ASSERT_INT_EQ(th_enter_temp_dir(saved_cwd, sizeof(saved_cwd), tmpdir), 0);
    conv_branch_init();
    conv_branch_create("s1", "alpha", 5);
    conv_branch_create("s2", "beta", 10);
    ASSERT_INT_EQ(conv_branch_find("alpha"), 0);
    ASSERT_INT_EQ(conv_branch_find("beta"), 1);
    ASSERT_INT_EQ(conv_branch_find("gamma"), -1);
    conv_branch_free();
    ASSERT_INT_EQ(th_leave_temp_dir(saved_cwd, tmpdir), 0);
    return 0;
}

static int test_find_by_session_id(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-XXXXXX";
    ASSERT_INT_EQ(th_enter_temp_dir(saved_cwd, sizeof(saved_cwd), tmpdir), 0);
    conv_branch_init();
    conv_branch_create("session-123", "x", 1);
    ASSERT_INT_EQ(conv_branch_find("session-123"), 0);
    ASSERT_INT_EQ(conv_branch_find("session-999"), -1);
    conv_branch_free();
    ASSERT_INT_EQ(th_leave_temp_dir(saved_cwd, tmpdir), 0);
    return 0;
}

static int test_find_by_index(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-XXXXXX";
    ASSERT_INT_EQ(th_enter_temp_dir(saved_cwd, sizeof(saved_cwd), tmpdir), 0);
    conv_branch_init();
    conv_branch_create("s1", "a", 1);
    conv_branch_create("s2", "b", 2);
    ASSERT_INT_EQ(conv_branch_find("1"), 0);
    ASSERT_INT_EQ(conv_branch_find("2"), 1);
    ASSERT_INT_EQ(conv_branch_find("0"), -1);
    ASSERT_INT_EQ(conv_branch_find("3"), -1);
    conv_branch_free();
    ASSERT_INT_EQ(th_leave_temp_dir(saved_cwd, tmpdir), 0);
    return 0;
}

static int test_format_list_empty(void) {
    conv_branch_init();
    char *s = conv_branch_format_list();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "no branches") != NULL);
    free(s);
    conv_branch_free();
    return 0;
}

static int test_format_list_with_entries(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-XXXXXX";
    ASSERT_INT_EQ(th_enter_temp_dir(saved_cwd, sizeof(saved_cwd), tmpdir), 0);
    conv_branch_init();
    conv_branch_create("s1", "feat-auth", 15);
    char *s = conv_branch_format_list();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "feat-auth") != NULL);
    ASSERT(strstr(s, "15 msgs") != NULL);
    free(s);
    conv_branch_free();
    ASSERT_INT_EQ(th_leave_temp_dir(saved_cwd, tmpdir), 0);
    return 0;
}

static int test_null_session_id(void) {
    conv_branch_init();
    int rc = conv_branch_create(NULL, "x", 1);
    ASSERT_INT_EQ(rc, -1);
    ASSERT_INT_EQ(conv_branch_count(), 0);
    conv_branch_free();
    return 0;
}

static int test_out_of_range_idx(void) {
    conv_branch_init();
    ASSERT_NULL(conv_branch_session_id(-1));
    ASSERT_NULL(conv_branch_session_id(0));
    ASSERT_NULL(conv_branch_name(999));
    ASSERT(conv_branch_has_snapshot(-1) == false);
    ASSERT_INT_EQ(conv_branch_msg_count(0), 0);
    conv_branch_free();
    return 0;
}

static int test_create_with_file_snapshot_repo(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-repo-XXXXXX";
    char homedir[] = "/tmp/nevercode-branch-home-XXXXXX";
    char note_path[NC_PATH_MAX];
    char draft_path[NC_PATH_MAX];
    char contents[128];
    char stash_list[NC_MSG_MAX];
    char *old_home = getenv(ENV_HOME) ? nc_xstrdup(getenv(ENV_HOME)) : NULL;
    conversation_t conv;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(mkdtemp(homedir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    setenv("HOME", homedir, 1);
    config_home_reset_cache();
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    {
        const char *mkdir_argv[] = {"mkdir", "-p", "scratch/deep", NULL};
        ASSERT_INT_EQ(th_run_cmd(tmpdir, mkdir_argv), 0);
    }
    snprintf(draft_path, sizeof(draft_path), "%s/scratch/deep/draft.txt", tmpdir);
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);
    ASSERT_INT_EQ(th_write_text(draft_path, "draft\n"), 0);

    conversation_init(&conv);
    conversation_add_user_text(&conv, "branch me");
    ASSERT_INT_EQ(history_save(&conv, "session-real"), 0);
    conv_branch_init();
    ASSERT_INT_EQ(conv_branch_create("session-real", "branch-real", 4), 0);
    ASSERT_INT_EQ(conv_branch_count(), 1);
    ASSERT_TRUE(conv_branch_has_snapshot(0));
    ASSERT_NOT_NULL(conv_branch_snapshot_ref(0));
    ASSERT_INT_EQ(history_attach_file_snapshot("session-real", conv_branch_snapshot_ref(0)), 0);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");
    ASSERT_INT_EQ(th_read_text(draft_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "draft\n");
    ASSERT_INT_EQ(git_stash_list(tmpdir, stash_list, sizeof(stash_list)), 0);
    ASSERT_STR_CONTAINS(stash_list, NC_BRANCH_STASH_PREFIX "session-real:branch-real");

    ASSERT_INT_EQ(git_restore_all_from_ref(tmpdir, "HEAD"), 0);
    {
        const char *rm_argv[] = {"rm", "-rf", "scratch", NULL};
        ASSERT_INT_EQ(th_run_cmd(tmpdir, rm_argv), 0);
    }
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "one\n");
    ASSERT_INT_EQ(history_restore_file_snapshot("session-real"), HISTORY_FILE_SNAPSHOT_RESTORED);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");
    ASSERT_INT_EQ(th_read_text(draft_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "draft\n");
    ASSERT_INT_EQ(history_restore_file_snapshot("session-real"), HISTORY_FILE_SNAPSHOT_NONE);
    conversation_free(&conv);
    conv_branch_free();

    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    if (old_home) {
        setenv("HOME", old_home, 1);
        free(old_home);
    } else
        unsetenv("HOME");
    config_home_reset_cache();
    ASSERT_INT_EQ(th_remove_tree(homedir), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_file_snapshot_restore_dirty_repo(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-dirty-repo-XXXXXX";
    char homedir[] = "/tmp/nevercode-branch-dirty-home-XXXXXX";
    char note_path[NC_PATH_MAX];
    char contents[128];
    char *old_home = getenv(ENV_HOME) ? nc_xstrdup(getenv(ENV_HOME)) : NULL;
    conversation_t conv;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(mkdtemp(homedir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    setenv("HOME", homedir, 1);
    config_home_reset_cache();
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    conversation_init(&conv);
    conversation_add_user_text(&conv, "branch me");
    ASSERT_INT_EQ(history_save(&conv, "session-dirty"), 0);
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);
    conv_branch_init();
    ASSERT_INT_EQ(conv_branch_create("session-dirty", "branch-dirty", 3), 0);
    ASSERT_NOT_NULL(conv_branch_snapshot_ref(0));
    ASSERT_INT_EQ(history_attach_file_snapshot("session-dirty", conv_branch_snapshot_ref(0)), 0);

    ASSERT_INT_EQ(th_write_text(note_path, "three\n"), 0);
    ASSERT_INT_EQ(history_restore_file_snapshot("session-dirty"),
                  HISTORY_FILE_SNAPSHOT_SKIPPED_DIRTY);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "three\n");

    conversation_free(&conv);
    conv_branch_free();
    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    if (old_home) {
        setenv("HOME", old_home, 1);
        free(old_home);
    } else
        unsetenv("HOME");
    config_home_reset_cache();
    ASSERT_INT_EQ(th_remove_tree(homedir), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_file_snapshot_restore_unavailable_outside_repo(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-branch-unavail-repo-XXXXXX";
    char homedir[] = "/tmp/nevercode-branch-unavail-home-XXXXXX";
    char nonrepo[] = "/tmp/nevercode-branch-unavail-work-XXXXXX";
    char *old_home = getenv(ENV_HOME) ? nc_xstrdup(getenv(ENV_HOME)) : NULL;
    conversation_t conv;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(mkdtemp(homedir));
    ASSERT_NOT_NULL(mkdtemp(nonrepo));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    setenv("HOME", homedir, 1);
    config_home_reset_cache();
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    conversation_init(&conv);
    conversation_add_user_text(&conv, "branch me");
    ASSERT_INT_EQ(history_save(&conv, "session-unavail"), 0);
    conv_branch_init();
    ASSERT_INT_EQ(th_write_text("note.txt", "two\n"), 0);
    ASSERT_INT_EQ(conv_branch_create("session-unavail", "branch-unavail", 2), 0);
    ASSERT_NOT_NULL(conv_branch_snapshot_ref(0));
    ASSERT_INT_EQ(history_attach_file_snapshot("session-unavail", conv_branch_snapshot_ref(0)), 0);

    ASSERT_INT_EQ(chdir(nonrepo), 0);
    ASSERT_INT_EQ(history_restore_file_snapshot("session-unavail"),
                  HISTORY_FILE_SNAPSHOT_UNAVAILABLE);

    conversation_free(&conv);
    conv_branch_free();
    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    if (old_home) {
        setenv("HOME", old_home, 1);
        free(old_home);
    } else
        unsetenv("HOME");
    config_home_reset_cache();
    ASSERT_INT_EQ(th_remove_tree(nonrepo), 0);
    ASSERT_INT_EQ(th_remove_tree(homedir), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

int test_conv_branch_all(void) {
    TEST_SUITE("conv_branch");
    TEST_COUNTERS;

    RUN_TEST(test_init_free);
    RUN_TEST(test_create_basic);
    RUN_TEST(test_create_unnamed);
    RUN_TEST(test_create_multiple);
    RUN_TEST(test_find_by_name);
    RUN_TEST(test_find_by_session_id);
    RUN_TEST(test_find_by_index);
    RUN_TEST(test_format_list_empty);
    RUN_TEST(test_format_list_with_entries);
    RUN_TEST(test_null_session_id);
    RUN_TEST(test_out_of_range_idx);
    RUN_TEST(test_create_with_file_snapshot_repo);
    RUN_TEST(test_file_snapshot_restore_dirty_repo);
    RUN_TEST(test_file_snapshot_restore_unavailable_outside_repo);

    TEST_RETURN_FAILED();
}
