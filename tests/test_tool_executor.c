#include "test.h"
#include "test_helpers.h"
#include "repl.h"
#include "conversation.h"
#include "tools.h"
#include "git_ops.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void setup_executor_env(config_t *cfg) {
    config_init(cfg);
    g_cfg_ptr = cfg;
    perm_init(&g_perms);
    hooks_init(&g_hooks);
    read_file_tracker_init(&g_read_tracker);
    approval_cache_init();
    vtrig_init();
    file_history_init();
    git_undo_init();
    tool_bash_register();
}

static void teardown_executor_env(config_t *cfg) {
    git_undo_free();
    file_history_free();
    approval_cache_free();
    read_file_tracker_free(&g_read_tracker);
    hooks_free(&g_hooks);
    perm_free(&g_perms);
    config_free(cfg);
    g_cfg_ptr = NULL;
}

static void enqueue_bash(stream_state_t *st, const char *id, const char *command,
                         const char *working_directory) {
    cJSON *input = cJSON_CreateObject();
    char *json = NULL;
    int idx = st->num_tc++;
    nc_strlcpy(st->pending_tc[idx].id, id, sizeof(st->pending_tc[idx].id));
    nc_strlcpy(st->pending_tc[idx].name, "Bash", sizeof(st->pending_tc[idx].name));
    strbuf_init(&st->pending_tc[idx].args);
    cJSON_AddStringToObject(input, "command", command);
    if (working_directory)
        cJSON_AddStringToObject(input, "working_directory", working_directory);
    json = cJSON_PrintUnformatted(input);
    if (json)
        strbuf_append_str(&st->pending_tc[idx].args, json);
    free(json);
    cJSON_Delete(input);
}

static int test_no_pending(void) {
    conversation_t conv;
    conversation_init(&conv);
    stream_state_t st;
    stream_state_init(&st);
    st.num_tc = 0;
    execute_pending_tools(&conv, &st);
    ASSERT_INT_EQ(st.num_tc, 0);
    stream_state_free(&st);
    conversation_free(&conv);
    return 0;
}

static int test_empty_conv(void) {
    conversation_t conv;
    conversation_init(&conv);
    ASSERT_INT_EQ(conv.len, 0);
    stream_state_t st;
    stream_state_init(&st);
    execute_pending_tools(&conv, &st);
    stream_state_free(&st);
    conversation_free(&conv);
    return 0;
}

static int test_state_lifecycle(void) {
    stream_state_t st;
    stream_state_init(&st);
    ASSERT_INT_EQ(st.num_tc, 0);
    ASSERT_INT_EQ(st.input_tokens, 0);
    ASSERT_INT_EQ(st.output_tokens, 0);
    ASSERT_FALSE(st.had_error);
    stream_state_free(&st);
    return 0;
}

static int test_bash_relative_paths_restore_redirection_and_delete(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char old_path[NC_PATH_MAX];
    char note_path[NC_PATH_MAX];
    char scratch_dir[NC_PATH_MAX];
    char contents[128];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(old_path, sizeof(old_path), "%s/keep/old.txt", tmpdir);
    snprintf(note_path, sizeof(note_path), "%s/scratch/deep/note.txt", tmpdir);
    snprintf(scratch_dir, sizeof(scratch_dir), "%s/scratch", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/keep", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(th_write_text(old_path, "keep me\n"), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-1",
                 "mkdir -p scratch/deep && printf 'draft\\n' > scratch/deep/note.txt && rm "
                 "keep/old.txt",
                 tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_FALSE(access(old_path, F_OK) == 0);
    ASSERT_TRUE(access(note_path, F_OK) == 0);
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 3);
        ASSERT_INT_EQ(restored, 3);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_INT_EQ(th_read_text(old_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "keep me\n");
    ASSERT_FALSE(access(note_path, F_OK) == 0);
    ASSERT_FALSE(access(scratch_dir, F_OK) == 0);

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_relative_paths_restore_move_targets(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char source_path[NC_PATH_MAX];
    char target_path[NC_PATH_MAX];
    char target_dir[NC_PATH_MAX];
    char contents[128];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(source_path, sizeof(source_path), "%s/src/item.txt", tmpdir);
    snprintf(target_path, sizeof(target_path), "%s/dest/item.txt", tmpdir);
    snprintf(target_dir, sizeof(target_dir), "%s/dest", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/src", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(th_write_text(source_path, "move me\n"), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-2", "mkdir -p dest && mv src/item.txt dest/item.txt", tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_FALSE(access(source_path, F_OK) == 0);
    ASSERT_TRUE(access(target_path, F_OK) == 0);
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 3);
        ASSERT_INT_EQ(restored, 3);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_INT_EQ(th_read_text(source_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "move me\n");
    ASSERT_FALSE(access(target_path, F_OK) == 0);
    ASSERT_FALSE(access(target_dir, F_OK) == 0);

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_relative_paths_restore_new_directory_tree(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char deep_dir[NC_PATH_MAX];
    char scratch_dir[NC_PATH_MAX];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(deep_dir, sizeof(deep_dir), "%s/scratch/deep", tmpdir);
    snprintf(scratch_dir, sizeof(scratch_dir), "%s/scratch", tmpdir);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-3", "mkdir -p scratch/deep", tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_TRUE(access(deep_dir, F_OK) == 0);
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 1);
        ASSERT_INT_EQ(restored, 1);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_FALSE(access(deep_dir, F_OK) == 0);
    ASSERT_FALSE(access(scratch_dir, F_OK) == 0);

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_relative_paths_restore_deleted_directory_tree(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char root_dir[NC_PATH_MAX];
    char nested_file[NC_PATH_MAX];
    char contents[128];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(root_dir, sizeof(root_dir), "%s/tree", tmpdir);
    snprintf(nested_file, sizeof(nested_file), "%s/tree/sub/note.txt", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/tree/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(th_write_text(nested_file, "forest\n"), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-4", "rm -rf tree", tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_FALSE(access(root_dir, F_OK) == 0);
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 3);
        ASSERT_INT_EQ(restored, 3);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_INT_EQ(th_read_text(nested_file, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "forest\n");

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_relative_paths_restore_directory_move_into_dir(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char source_root[NC_PATH_MAX];
    char source_file[NC_PATH_MAX];
    char moved_file[NC_PATH_MAX];
    char archive_dir[NC_PATH_MAX];
    char contents[128];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(source_root, sizeof(source_root), "%s/src", tmpdir);
    snprintf(source_file, sizeof(source_file), "%s/src/sub/note.txt", tmpdir);
    snprintf(moved_file, sizeof(moved_file), "%s/archive/src/sub/note.txt", tmpdir);
    snprintf(archive_dir, sizeof(archive_dir), "%s/archive", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/src/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(nc_mkdir_p(archive_dir), 0);
    ASSERT_INT_EQ(th_write_text(source_file, "move dir\n"), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-5", "mv src archive", tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_FALSE(access(source_root, F_OK) == 0);
    ASSERT_TRUE(access(moved_file, F_OK) == 0);
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 4);
        ASSERT_INT_EQ(restored, 4);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_INT_EQ(th_read_text(source_file, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "move dir\n");
    ASSERT_FALSE(access(moved_file, F_OK) == 0);
    ASSERT_TRUE(access(archive_dir, F_OK) == 0);

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_relative_paths_restore_directory_copy_into_dir(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char source_file[NC_PATH_MAX];
    char copied_file[NC_PATH_MAX];
    char copies_dir[NC_PATH_MAX];
    char contents[128];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(source_file, sizeof(source_file), "%s/tree/sub/note.txt", tmpdir);
    snprintf(copied_file, sizeof(copied_file), "%s/copies/tree/sub/note.txt", tmpdir);
    snprintf(copies_dir, sizeof(copies_dir), "%s/copies", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/tree/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(nc_mkdir_p(copies_dir), 0);
    ASSERT_INT_EQ(th_write_text(source_file, "copy dir\n"), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-6", "cp -r tree copies", tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_TRUE(access(source_file, F_OK) == 0);
    ASSERT_TRUE(access(copied_file, F_OK) == 0);
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 1);
        ASSERT_INT_EQ(restored, 1);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_INT_EQ(th_read_text(source_file, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "copy dir\n");
    ASSERT_FALSE(access(copied_file, F_OK) == 0);
    ASSERT_TRUE(access(copies_dir, F_OK) == 0);

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_relative_paths_restore_directory_copy_over_existing_child(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char source_file[NC_PATH_MAX];
    char existing_file[NC_PATH_MAX];
    char contents[128];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(source_file, sizeof(source_file), "%s/tree/sub/note.txt", tmpdir);
    snprintf(existing_file, sizeof(existing_file), "%s/copies/tree/sub/note.txt", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/tree/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
        snprintf(parent, sizeof(parent), "%s/copies/tree/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(th_write_text(source_file, "new\n"), 0);
    ASSERT_INT_EQ(th_write_text(existing_file, "old\n"), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-7", "cp -r tree copies", tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_INT_EQ(th_read_text(existing_file, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "new\n");
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 3);
        ASSERT_INT_EQ(restored, 3);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_INT_EQ(th_read_text(existing_file, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "old\n");

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_relative_paths_restore_multi_source_directory_copy(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char first_source[NC_PATH_MAX];
    char second_source[NC_PATH_MAX];
    char copied_first[NC_PATH_MAX];
    char copied_second[NC_PATH_MAX];
    char bucket_dir[NC_PATH_MAX];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(first_source, sizeof(first_source), "%s/a/sub/one.txt", tmpdir);
    snprintf(second_source, sizeof(second_source), "%s/b/sub/two.txt", tmpdir);
    snprintf(copied_first, sizeof(copied_first), "%s/bucket/a/sub/one.txt", tmpdir);
    snprintf(copied_second, sizeof(copied_second), "%s/bucket/b/sub/two.txt", tmpdir);
    snprintf(bucket_dir, sizeof(bucket_dir), "%s/bucket", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/a/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
        snprintf(parent, sizeof(parent), "%s/b/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(nc_mkdir_p(bucket_dir), 0);
    ASSERT_INT_EQ(th_write_text(first_source, "one\n"), 0);
    ASSERT_INT_EQ(th_write_text(second_source, "two\n"), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-8", "cp -r a b bucket", tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_TRUE(access(copied_first, F_OK) == 0);
    ASSERT_TRUE(access(copied_second, F_OK) == 0);
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 2);
        ASSERT_INT_EQ(restored, 2);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_FALSE(access(copied_first, F_OK) == 0);
    ASSERT_FALSE(access(copied_second, F_OK) == 0);
    ASSERT_TRUE(access(bucket_dir, F_OK) == 0);

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_relative_paths_restore_move_over_existing_file(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char source_file[NC_PATH_MAX];
    char existing_file[NC_PATH_MAX];
    char contents[128];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(source_file, sizeof(source_file), "%s/src/note.txt", tmpdir);
    snprintf(existing_file, sizeof(existing_file), "%s/archive/note.txt", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/src", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
        snprintf(parent, sizeof(parent), "%s/archive", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(th_write_text(source_file, "new-move\n"), 0);
    ASSERT_INT_EQ(th_write_text(existing_file, "old-keep\n"), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-9", "mv src/note.txt archive/note.txt", tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_FALSE(access(source_file, F_OK) == 0);
    ASSERT_INT_EQ(th_read_text(existing_file, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "new-move\n");
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 2);
        ASSERT_INT_EQ(restored, 2);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_INT_EQ(th_read_text(source_file, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "new-move\n");
    ASSERT_INT_EQ(th_read_text(existing_file, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "old-keep\n");

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_relative_paths_restore_multi_source_directory_move(void) {
    char tmpdir[] = "/tmp/nevercode-tool-exec-XXXXXX";
    char first_source[NC_PATH_MAX];
    char second_source[NC_PATH_MAX];
    char moved_first[NC_PATH_MAX];
    char moved_second[NC_PATH_MAX];
    char bucket_dir[NC_PATH_MAX];
    char contents[128];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(first_source, sizeof(first_source), "%s/a/sub/one.txt", tmpdir);
    snprintf(second_source, sizeof(second_source), "%s/b/sub/two.txt", tmpdir);
    snprintf(moved_first, sizeof(moved_first), "%s/bucket/a/sub/one.txt", tmpdir);
    snprintf(moved_second, sizeof(moved_second), "%s/bucket/b/sub/two.txt", tmpdir);
    snprintf(bucket_dir, sizeof(bucket_dir), "%s/bucket", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/a/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
        snprintf(parent, sizeof(parent), "%s/b/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(nc_mkdir_p(bucket_dir), 0);
    ASSERT_INT_EQ(th_write_text(first_source, "one-move\n"), 0);
    ASSERT_INT_EQ(th_write_text(second_source, "two-move\n"), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-10", "mv a b bucket", tmpdir);
    execute_pending_tools(&conv, &st);

    ASSERT_FALSE(access(first_source, F_OK) == 0);
    ASSERT_FALSE(access(second_source, F_OK) == 0);
    ASSERT_TRUE(access(moved_first, F_OK) == 0);
    ASSERT_TRUE(access(moved_second, F_OK) == 0);
    {
        int restored = 0, errors = 0;
        ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 8);
        ASSERT_INT_EQ(restored, 8);
        ASSERT_INT_EQ(errors, 0);
    }
    ASSERT_INT_EQ(th_read_text(first_source, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "one-move\n");
    ASSERT_INT_EQ(th_read_text(second_source, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two-move\n");
    ASSERT_FALSE(access(moved_first, F_OK) == 0);
    ASSERT_FALSE(access(moved_second, F_OK) == 0);
    ASSERT_TRUE(access(bucket_dir, F_OK) == 0);

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_bash_skips_git_undo_for_external_workdir(void) {
    char saved_cwd[NC_PATH_MAX];
    char repodir[] = "/tmp/nevercode-tool-exec-repo-XXXXXX";
    char workdir[] = "/tmp/nevercode-tool-exec-work-XXXXXX";
    char note_path[NC_PATH_MAX];
    char draft_path[NC_PATH_MAX];
    char contents[128];
    char stash_list[NC_MSG_MAX];
    conversation_t conv;
    stream_state_t st;
    config_t cfg;

    th_setup_git_env();
    ASSERT_NOT_NULL(mkdtemp(repodir));
    ASSERT_NOT_NULL(mkdtemp(workdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(repodir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", repodir);
    ASSERT_INT_EQ(th_write_text(note_path, "dirty\n"), 0);
    ASSERT_INT_EQ(chdir(repodir), 0);

    setup_executor_env(&cfg);
    file_history_next_turn();
    conversation_init(&conv);
    stream_state_init(&st);
    enqueue_bash(&st, "bash-external", "printf 'draft\\n' > draft.txt", workdir);
    execute_pending_tools(&conv, &st);

    snprintf(draft_path, sizeof(draft_path), "%s/draft.txt", workdir);
    ASSERT_INT_EQ(git_undo_count(), 0);
    ASSERT_FALSE(git_undo_has_turn(1));
    ASSERT_INT_EQ(git_stash_list(repodir, stash_list, sizeof(stash_list)), 0);
    ASSERT_STR_EQ(stash_list, "");
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "dirty\n");
    ASSERT_INT_EQ(th_read_text(draft_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "draft\n");

    stream_state_free(&st);
    conversation_free(&conv);
    teardown_executor_env(&cfg);
    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(workdir), 0);
    ASSERT_INT_EQ(th_remove_tree(repodir), 0);
    return 0;
}

int test_tool_executor_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_executor");
    RUN_TEST(test_no_pending);
    RUN_TEST(test_empty_conv);
    RUN_TEST(test_state_lifecycle);
    RUN_TEST(test_bash_relative_paths_restore_redirection_and_delete);
    RUN_TEST(test_bash_relative_paths_restore_move_targets);
    RUN_TEST(test_bash_relative_paths_restore_new_directory_tree);
    RUN_TEST(test_bash_relative_paths_restore_deleted_directory_tree);
    RUN_TEST(test_bash_relative_paths_restore_directory_move_into_dir);
    RUN_TEST(test_bash_relative_paths_restore_directory_copy_into_dir);
    RUN_TEST(test_bash_relative_paths_restore_directory_copy_over_existing_child);
    RUN_TEST(test_bash_relative_paths_restore_multi_source_directory_copy);
    RUN_TEST(test_bash_relative_paths_restore_move_over_existing_file);
    RUN_TEST(test_bash_relative_paths_restore_multi_source_directory_move);
    RUN_TEST(test_bash_skips_git_undo_for_external_workdir);
    TEST_RETURN_FAILED();
}
