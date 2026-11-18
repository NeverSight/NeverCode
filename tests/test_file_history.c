#include "test.h"
#include "test_helpers.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_fh_init(void) {
    file_history_init();
    ASSERT_INT_EQ(file_history_count(), 0);
    ASSERT_INT_EQ(file_history_current_turn(), 0);
    file_history_free();
    return 0;
}

static int test_fh_next_turn(void) {
    file_history_init();
    ASSERT_INT_EQ(file_history_current_turn(), 0);
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_current_turn(), 1);
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_current_turn(), 2);
    file_history_free();
    return 0;
}

static int test_fh_snapshot_new_file(void) {
    file_history_init();
    file_history_next_turn();
    int rc = file_history_snapshot("/tmp/nevercode_test_fh_nonexist_99.txt");
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(file_history_count(), 1);
    file_history_free();
    return 0;
}

static int test_fh_snapshot_existing(void) {
    file_history_init();
    file_history_next_turn();
    const char *path = "/tmp/nevercode_test_fh_exist.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "hello world\n");
    fclose(f);
    int rc = file_history_snapshot(path);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(file_history_count(), 1);
    unlink(path);
    file_history_free();
    return 0;
}

static int test_fh_dedup_same_turn(void) {
    file_history_init();
    file_history_next_turn();
    const char *path = "/tmp/nevercode_test_fh_dedup.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "data");
    fclose(f);
    file_history_snapshot(path);
    file_history_snapshot(path);
    ASSERT_INT_EQ(file_history_count(), 1);
    unlink(path);
    file_history_free();
    return 0;
}

static int test_fh_restore_turn(void) {
    file_history_init();
    file_history_next_turn();
    const char *path = "/tmp/nevercode_test_fh_restore.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "original content");
    fclose(f);
    file_history_snapshot(path);

    f = fopen(path, "w");
    fprintf(f, "modified content");
    fclose(f);

    int restored = 0, errors = 0;
    file_history_restore_turn(1, &restored, &errors);
    ASSERT_INT_EQ(restored, 1);
    ASSERT_INT_EQ(errors, 0);

    f = fopen(path, "r");
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    ASSERT_STR_EQ(buf, "original content");
    unlink(path);
    file_history_free();
    return 0;
}

static int test_fh_restore_nonexistent_turn(void) {
    file_history_init();
    int restored = 0, errors = 0;
    file_history_restore_turn(999, &restored, &errors);
    ASSERT_INT_EQ(restored, 0);
    ASSERT_INT_EQ(errors, 0);
    file_history_free();
    return 0;
}

static int test_fh_unique_turns(void) {
    file_history_init();
    const char *p1 = "/tmp/nevercode_test_fh_t1.txt";
    const char *p2 = "/tmp/nevercode_test_fh_t2.txt";
    FILE *f = fopen(p1, "w");
    fprintf(f, "a");
    fclose(f);
    f = fopen(p2, "w");
    fprintf(f, "b");
    fclose(f);

    file_history_next_turn();
    file_history_snapshot(p1);
    file_history_next_turn();
    file_history_snapshot(p2);

    int turns[16];
    int nt = file_history_unique_turns(turns, 16);
    ASSERT_INT_EQ(nt, 2);
    ASSERT_INT_EQ(turns[0], 1);
    ASSERT_INT_EQ(turns[1], 2);
    unlink(p1);
    unlink(p2);
    file_history_free();
    return 0;
}

static int test_fh_format_checkpoints(void) {
    file_history_init();
    char *cp = file_history_format_checkpoints();
    ASSERT(strstr(cp, "no file changes") != NULL);
    free(cp);

    const char *path = "/tmp/nevercode_test_fh_fmt.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "data");
    fclose(f);
    file_history_next_turn();
    file_history_snapshot(path);

    cp = file_history_format_checkpoints();
    ASSERT(strstr(cp, "Turn 1") != NULL);
    ASSERT(strstr(cp, path) != NULL);
    free(cp);
    unlink(path);
    file_history_free();
    return 0;
}

static int test_fh_files_at_turn(void) {
    file_history_init();
    const char *p1 = "/tmp/nevercode_test_fh_fa1.txt";
    const char *p2 = "/tmp/nevercode_test_fh_fa2.txt";
    FILE *f;
    f = fopen(p1, "w");
    fprintf(f, "x");
    fclose(f);
    f = fopen(p2, "w");
    fprintf(f, "y");
    fclose(f);

    file_history_next_turn();
    file_history_snapshot(p1);
    file_history_snapshot(p2);

    const char *paths[8];
    int n = file_history_files_at_turn(1, paths, 8);
    ASSERT_INT_EQ(n, 2);
    unlink(p1);
    unlink(p2);
    file_history_free();
    return 0;
}

static int test_fh_null_path(void) {
    file_history_init();
    ASSERT_INT_EQ(file_history_snapshot(NULL), -1);
    file_history_free();
    return 0;
}

static int test_fh_restore_recreates_missing_parents(void) {
    char tmpdir[] = "/tmp/nevercode-fh-XXXXXX";
    char path[NC_PATH_MAX];
    char contents[128];
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(path, sizeof(path), "%s/nested/deep/note.txt", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/nested/deep", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(th_write_text(path, "original\n"), 0);

    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(path), 0);
    {
        char subtree[NC_PATH_MAX];
        snprintf(subtree, sizeof(subtree), "%s/nested", tmpdir);
        ASSERT_INT_EQ(th_remove_tree(subtree), 0);
    }
    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 1);
    ASSERT_INT_EQ(restored, 1);
    ASSERT_INT_EQ(errors, 0);
    ASSERT_INT_EQ(th_read_text(path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "original\n");
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_fh_restore_new_nested_file_prunes_dirs(void) {
    char tmpdir[] = "/tmp/nevercode-fh-XXXXXX";
    char path[NC_PATH_MAX];
    char scratch[NC_PATH_MAX];
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(path, sizeof(path), "%s/scratch/deep/new.txt", tmpdir);
    snprintf(scratch, sizeof(scratch), "%s/scratch", tmpdir);

    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(path), 0);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/scratch/deep", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(th_write_text(path, "draft\n"), 0);
    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 1);
    ASSERT_INT_EQ(restored, 1);
    ASSERT_INT_EQ(errors, 0);
    ASSERT_FALSE(access(path, F_OK) == 0);
    ASSERT_FALSE(access(scratch, F_OK) == 0);
    ASSERT_TRUE(access(tmpdir, F_OK) == 0);
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_fh_restore_new_file_keeps_existing_parent(void) {
    char tmpdir[] = "/tmp/nevercode-fh-XXXXXX";
    char path[NC_PATH_MAX];
    char scratch[NC_PATH_MAX];
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(scratch, sizeof(scratch), "%s/scratch", tmpdir);
    snprintf(path, sizeof(path), "%s/scratch/new.txt", tmpdir);
    ASSERT_INT_EQ(nc_mkdir_p(scratch), 0);

    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(path), 0);
    ASSERT_INT_EQ(th_write_text(path, "draft\n"), 0);
    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 1);
    ASSERT_INT_EQ(restored, 1);
    ASSERT_INT_EQ(errors, 0);
    ASSERT_FALSE(access(path, F_OK) == 0);
    ASSERT_TRUE(access(scratch, F_OK) == 0);
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_fh_restore_file_over_directory_tree(void) {
    char tmpdir[] = "/tmp/nevercode-fh-XXXXXX";
    char path[NC_PATH_MAX];
    char nested[NC_PATH_MAX];
    char contents[128];
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(path, sizeof(path), "%s/note.txt", tmpdir);
    ASSERT_INT_EQ(th_write_text(path, "original\n"), 0);

    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(path), 0);
    ASSERT_INT_EQ(unlink(path), 0);
    ASSERT_INT_EQ(nc_mkdir_p(path), 0);
    snprintf(nested, sizeof(nested), "%.4083s/child.txt", path);
    ASSERT_INT_EQ(th_write_text(nested, "nested\n"), 0);
    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 1);
    ASSERT_INT_EQ(restored, 1);
    ASSERT_INT_EQ(errors, 0);
    ASSERT_INT_EQ(th_read_text(path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "original\n");
    ASSERT_FALSE(access(nested, F_OK) == 0);
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_fh_restore_multi_path_same_turn(void) {
    char tmpdir[] = "/tmp/nevercode-fh-XXXXXX";
    char existing[NC_PATH_MAX];
    char fresh[NC_PATH_MAX];
    char fresh_dir[NC_PATH_MAX];
    char contents[128];
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(existing, sizeof(existing), "%s/a/keep.txt", tmpdir);
    snprintf(fresh, sizeof(fresh), "%s/b/new.txt", tmpdir);
    snprintf(fresh_dir, sizeof(fresh_dir), "%s/b", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/a", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(th_write_text(existing, "keep\n"), 0);

    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(existing), 0);
    ASSERT_INT_EQ(file_history_snapshot(fresh), 0);
    ASSERT_INT_EQ(th_write_text(existing, "changed\n"), 0);
    ASSERT_INT_EQ(nc_mkdir_p(fresh_dir), 0);
    ASSERT_INT_EQ(th_write_text(fresh, "new\n"), 0);
    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 2);
    ASSERT_INT_EQ(restored, 2);
    ASSERT_INT_EQ(errors, 0);
    ASSERT_INT_EQ(th_read_text(existing, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "keep\n");
    ASSERT_FALSE(access(fresh, F_OK) == 0);
    ASSERT_FALSE(access(fresh_dir, F_OK) == 0);
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_fh_restore_deleted_empty_dir(void) {
    char tmpdir[] = "/tmp/nevercode-fh-XXXXXX";
    char dirpath[NC_PATH_MAX];
    struct stat st;
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(dirpath, sizeof(dirpath), "%s/empty/deep", tmpdir);
    ASSERT_INT_EQ(nc_mkdir_p(dirpath), 0);

    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(dirpath), 0);
    {
        char subtree[NC_PATH_MAX];
        snprintf(subtree, sizeof(subtree), "%s/empty", tmpdir);
        ASSERT_INT_EQ(th_remove_tree(subtree), 0);
    }

    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 1);
    ASSERT_INT_EQ(restored, 1);
    ASSERT_INT_EQ(errors, 0);
    ASSERT_INT_EQ(stat(dirpath, &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_fh_restore_deleted_directory_tree(void) {
    char tmpdir[] = "/tmp/nevercode-fh-XXXXXX";
    char root[NC_PATH_MAX];
    char nested[NC_PATH_MAX];
    char contents[128];
    struct stat st;
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(root, sizeof(root), "%s/tree", tmpdir);
    snprintf(nested, sizeof(nested), "%s/tree/sub/note.txt", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/tree/sub", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    ASSERT_INT_EQ(th_write_text(nested, "forest\n"), 0);

    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(root), 0);
    ASSERT_INT_EQ(file_history_count(), 3);
    ASSERT_INT_EQ(th_remove_tree(root), 0);

    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 3);
    ASSERT_INT_EQ(restored, 3);
    ASSERT_INT_EQ(errors, 0);
    ASSERT_INT_EQ(stat(root, &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_INT_EQ(th_read_text(nested, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "forest\n");
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_fh_restore_new_directory_prunes_parents(void) {
    char tmpdir[] = "/tmp/nevercode-fh-XXXXXX";
    char dirpath[NC_PATH_MAX];
    char scratch[NC_PATH_MAX];
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(dirpath, sizeof(dirpath), "%s/scratch/deep", tmpdir);
    snprintf(scratch, sizeof(scratch), "%s/scratch", tmpdir);

    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(dirpath), 0);
    ASSERT_INT_EQ(nc_mkdir_p(dirpath), 0);

    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 1);
    ASSERT_INT_EQ(restored, 1);
    ASSERT_INT_EQ(errors, 0);
    ASSERT_FALSE(access(dirpath, F_OK) == 0);
    ASSERT_FALSE(access(scratch, F_OK) == 0);
    ASSERT_TRUE(access(tmpdir, F_OK) == 0);
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_fh_format_directory_checkpoint(void) {
    char tmpdir[] = "/tmp/nevercode-fh-XXXXXX";
    char dirpath[NC_PATH_MAX];
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(dirpath, sizeof(dirpath), "%s/dironly", tmpdir);
    ASSERT_INT_EQ(nc_mkdir_p(dirpath), 0);

    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(dirpath), 0);
    char *cp = file_history_format_checkpoints();
    ASSERT_NOT_NULL(cp);
    ASSERT(strstr(cp, "[dir]") != NULL);
    ASSERT(strstr(cp, dirpath) != NULL);
    free(cp);
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

int test_file_history_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("file_history");
    RUN_TEST(test_fh_init);
    RUN_TEST(test_fh_next_turn);
    RUN_TEST(test_fh_snapshot_new_file);
    RUN_TEST(test_fh_snapshot_existing);
    RUN_TEST(test_fh_dedup_same_turn);
    RUN_TEST(test_fh_restore_turn);
    RUN_TEST(test_fh_restore_nonexistent_turn);
    RUN_TEST(test_fh_unique_turns);
    RUN_TEST(test_fh_format_checkpoints);
    RUN_TEST(test_fh_files_at_turn);
    RUN_TEST(test_fh_null_path);
    RUN_TEST(test_fh_restore_recreates_missing_parents);
    RUN_TEST(test_fh_restore_new_nested_file_prunes_dirs);
    RUN_TEST(test_fh_restore_new_file_keeps_existing_parent);
    RUN_TEST(test_fh_restore_file_over_directory_tree);
    RUN_TEST(test_fh_restore_multi_path_same_turn);
    RUN_TEST(test_fh_restore_deleted_empty_dir);
    RUN_TEST(test_fh_restore_deleted_directory_tree);
    RUN_TEST(test_fh_restore_new_directory_prunes_parents);
    RUN_TEST(test_fh_format_directory_checkpoint);
    TEST_RETURN_FAILED();
}
