#include "test.h"
#include "test_helpers.h"
#include "nevercode.h"
#include "git_ops.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int test_git_undo_init(void) {
    git_undo_init();
    ASSERT(git_undo_count() == 0);
    git_undo_free();
    return 0;
}

static int test_git_undo_available(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-git-undo-XXXXXX";
    const char *init_argv[] = {"git", "init", "-q", NULL};

    th_setup_git_env();
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_run_cmd(tmpdir, init_argv), 0);
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    git_undo_init();
    ASSERT_TRUE(git_undo_available());
    git_undo_free();
    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_git_undo_unavailable_outside_repo(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-git-undo-XXXXXX";

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    git_undo_init();
    ASSERT_FALSE(git_undo_available());
    git_undo_free();
    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(rmdir(tmpdir), 0);
    return 0;
}

static int test_git_undo_format_empty(void) {
    git_undo_init();
    char *list = git_undo_format_list();
    ASSERT_NOT_NULL(list);
    ASSERT(strstr(list, "No undo") != NULL || strstr(list, "checkpoint") != NULL);
    free(list);
    git_undo_free();
    return 0;
}

static int test_git_undo_restore_no_entry(void) {
    git_undo_init();
    int rc = git_undo_restore(999);
    ASSERT(rc == -1);
    git_undo_free();
    return 0;
}

static int test_git_undo_double_free(void) {
    git_undo_init();
    git_undo_free();
    git_undo_free(); /* should not crash */
    return 0;
}

static int test_git_undo_count_zero(void) {
    git_undo_init();
    ASSERT_INT_EQ(git_undo_count(), 0);
    git_undo_free();
    return 0;
}

static int test_git_undo_capture_and_restore(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-git-undo-XXXXXX";
    char note_path[NC_PATH_MAX];
    char contents[128];
    char stash_list[NC_MSG_MAX];

    th_setup_git_env();
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);

    git_undo_init();
    ASSERT_INT_EQ(git_undo_capture_turn(1, "FileWrite"), 0);
    ASSERT_INT_EQ(git_undo_count(), 1);
    ASSERT_TRUE(git_undo_has_turn(1));
    ASSERT_FALSE(git_undo_has_turn(2));
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");
    ASSERT_INT_EQ(git_stash_list(tmpdir, stash_list, sizeof(stash_list)), 0);
    ASSERT_STR_CONTAINS(stash_list, NC_STASH_MSG_PREFIX "1:FileWrite");

    ASSERT_INT_EQ(th_write_text(note_path, "three\n"), 0);
    ASSERT_INT_EQ(git_undo_restore(1), 0);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");
    git_undo_free();

    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_git_undo_capture_preserves_deleted_and_untracked(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-git-undo-XXXXXX";
    char note_path[NC_PATH_MAX];
    char keep_path[NC_PATH_MAX];
    char draft_path[NC_PATH_MAX];
    char contents[128];

    th_setup_git_env();
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    snprintf(keep_path, sizeof(keep_path), "%s/keep.txt", tmpdir);
    ASSERT_INT_EQ(th_write_text(keep_path, "keep\n"), 0);
    {
        const char *add_argv[] = {"git", "add", "keep.txt", NULL};
        const char *commit_argv[] = {"git", "commit", "-q", "-m", "keep", NULL};
        ASSERT_INT_EQ(th_run_cmd(tmpdir, add_argv), 0);
        ASSERT_INT_EQ(th_run_cmd(tmpdir, commit_argv), 0);
    }
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    snprintf(draft_path, sizeof(draft_path), "%s/draft.txt", tmpdir);
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);
    ASSERT_INT_EQ(unlink(keep_path), 0);
    ASSERT_INT_EQ(th_write_text(draft_path, "draft\n"), 0);

    git_undo_init();
    ASSERT_INT_EQ(git_undo_capture_turn(1, "Bash"), 0);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");
    ASSERT_FALSE(access(keep_path, F_OK) == 0);
    ASSERT_INT_EQ(th_read_text(draft_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "draft\n");
    git_undo_free();

    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_git_undo_restore_exact_turn_state(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-git-undo-XXXXXX";
    char note_path[NC_PATH_MAX];
    char keep_path[NC_PATH_MAX];
    char draft_path[NC_PATH_MAX];
    char extra_path[NC_PATH_MAX];
    char contents[128];

    th_setup_git_env();
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    snprintf(keep_path, sizeof(keep_path), "%s/keep.txt", tmpdir);
    ASSERT_INT_EQ(th_write_text(keep_path, "keep\n"), 0);
    {
        const char *add_argv[] = {"git", "add", "keep.txt", NULL};
        const char *commit_argv[] = {"git", "commit", "-q", "-m", "keep", NULL};
        ASSERT_INT_EQ(th_run_cmd(tmpdir, add_argv), 0);
        ASSERT_INT_EQ(th_run_cmd(tmpdir, commit_argv), 0);
    }
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    snprintf(draft_path, sizeof(draft_path), "%s/draft.txt", tmpdir);
    snprintf(extra_path, sizeof(extra_path), "%s/extra.txt", tmpdir);
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);
    ASSERT_INT_EQ(unlink(keep_path), 0);
    ASSERT_INT_EQ(th_write_text(draft_path, "draft\n"), 0);

    git_undo_init();
    ASSERT_INT_EQ(git_undo_capture_turn(1, "Bash"), 0);
    ASSERT_INT_EQ(th_write_text(note_path, "three\n"), 0);
    ASSERT_INT_EQ(th_write_text(keep_path, "recreated\n"), 0);
    ASSERT_INT_EQ(unlink(draft_path), 0);
    ASSERT_INT_EQ(th_write_text(extra_path, "extra\n"), 0);

    ASSERT_INT_EQ(git_undo_restore(1), 0);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");
    ASSERT_FALSE(access(keep_path, F_OK) == 0);
    ASSERT_INT_EQ(th_read_text(draft_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "draft\n");
    ASSERT_FALSE(access(extra_path, F_OK) == 0);
    git_undo_free();

    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_git_undo_restore_nested_directory_state(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-git-undo-XXXXXX";
    char tracked_path[NC_PATH_MAX];
    char nested_path[NC_PATH_MAX];
    char stray_path[NC_PATH_MAX];
    char contents[128];

    th_setup_git_env();
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    {
        const char *mkdir_argv[] = {"mkdir", "-p", "tracked/sub", NULL};
        const char *add_argv[] = {"git", "add", "tracked/sub/base.txt", NULL};
        const char *commit_argv[] = {"git", "commit", "-q", "-m", "nested-base", NULL};
        ASSERT_INT_EQ(th_run_cmd(tmpdir, mkdir_argv), 0);
        snprintf(tracked_path, sizeof(tracked_path), "%s/tracked/sub/base.txt", tmpdir);
        ASSERT_INT_EQ(th_write_text(tracked_path, "base\n"), 0);
        ASSERT_INT_EQ(th_run_cmd(tmpdir, add_argv), 0);
        ASSERT_INT_EQ(th_run_cmd(tmpdir, commit_argv), 0);
    }
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    {
        const char *mkdir_argv[] = {"mkdir", "-p", "scratch/deep", NULL};
        ASSERT_INT_EQ(th_run_cmd(tmpdir, mkdir_argv), 0);
    }
    snprintf(nested_path, sizeof(nested_path), "%s/scratch/deep/new.txt", tmpdir);
    snprintf(stray_path, sizeof(stray_path), "%s/stray/later/extra.txt", tmpdir);
    ASSERT_INT_EQ(unlink(tracked_path), 0);
    ASSERT_INT_EQ(th_write_text(nested_path, "draft\n"), 0);

    git_undo_init();
    ASSERT_INT_EQ(git_undo_capture_turn(1, "Bash"), 0);
    ASSERT_FALSE(access(tracked_path, F_OK) == 0);
    ASSERT_INT_EQ(th_read_text(nested_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "draft\n");

    ASSERT_INT_EQ(th_write_text("note.txt", "changed\n"), 0);
    {
        const char *mkdir_tracked_argv[] = {"mkdir", "-p", "tracked/sub", NULL};
        const char *mkdir_argv[] = {"mkdir", "-p", "stray/later", NULL};
        ASSERT_INT_EQ(th_run_cmd(tmpdir, mkdir_tracked_argv), 0);
        ASSERT_INT_EQ(th_run_cmd(tmpdir, mkdir_argv), 0);
    }
    ASSERT_INT_EQ(th_write_text(tracked_path, "recreated\n"), 0);
    ASSERT_INT_EQ(unlink(nested_path), 0);
    ASSERT_INT_EQ(th_write_text(stray_path, "extra\n"), 0);

    ASSERT_INT_EQ(git_undo_restore(1), 0);
    ASSERT_FALSE(access(tracked_path, F_OK) == 0);
    ASSERT_INT_EQ(th_read_text(nested_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "draft\n");
    ASSERT_FALSE(access(stray_path, F_OK) == 0);
    ASSERT_INT_EQ(th_read_text("note.txt", contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "one\n");
    git_undo_free();

    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_git_undo_capture_with_long_stash_list(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-git-undo-XXXXXX";
    char note_path[NC_PATH_MAX];
    char contents[128];
    char stash_list[2048];

    th_setup_git_env();
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    for (int i = 0; i < 13; i++) {
        char message[64];
        char body[64];
        const char *stash_argv[] = {"git", "stash", "push", "-u", "-m", message, NULL};
        snprintf(message, sizeof(message), "seed-%02d", i);
        snprintf(body, sizeof(body), "seed-%02d\n", i);
        ASSERT_INT_EQ(th_write_text(note_path, body), 0);
        ASSERT_INT_EQ(th_run_cmd(tmpdir, stash_argv), 0);
    }

    ASSERT_INT_EQ(chdir(tmpdir), 0);
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);

    git_undo_init();
    ASSERT_INT_EQ(git_undo_capture_turn(1, "Bash"), 0);
    ASSERT_INT_EQ(git_undo_count(), 1);
    ASSERT_TRUE(git_undo_has_turn(1));
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");
    ASSERT_INT_EQ(git_stash_list(tmpdir, stash_list, sizeof(stash_list)), 0);
    ASSERT_STR_CONTAINS(stash_list, NC_STASH_MSG_PREFIX "1:Bash");
    git_undo_free();

    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

int test_git_undo_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("git_undo");
    RUN_TEST(test_git_undo_init);
    RUN_TEST(test_git_undo_available);
    RUN_TEST(test_git_undo_unavailable_outside_repo);
    RUN_TEST(test_git_undo_format_empty);
    RUN_TEST(test_git_undo_restore_no_entry);
    RUN_TEST(test_git_undo_double_free);
    RUN_TEST(test_git_undo_count_zero);
    RUN_TEST(test_git_undo_capture_and_restore);
    RUN_TEST(test_git_undo_capture_preserves_deleted_and_untracked);
    RUN_TEST(test_git_undo_restore_exact_turn_state);
    RUN_TEST(test_git_undo_restore_nested_directory_state);
    RUN_TEST(test_git_undo_capture_with_long_stash_list);
    TEST_RETURN_FAILED();
}
