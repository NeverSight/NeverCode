#include "test.h"
#include "test_helpers.h"
#include "nevercode.h"
#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_gs_init(void) {
    ghost_snapshot_init("session_test");
    ASSERT(ghost_snapshot_available() == 0 || ghost_snapshot_available() == 1);
    ASSERT_INT_EQ(ghost_snapshot_count(), 0);
    ghost_snapshot_free();
    return 0;
}

static int test_gs_zero(void) {
    ghost_snapshot_init("zero_test");
    ASSERT_INT_EQ(ghost_snapshot_count(), 0);
    ghost_snapshot_free();
    return 0;
}

static int test_gs_turn_bounds(void) {
    ghost_snapshot_init("turn_test");
    ASSERT_INT_EQ(ghost_snapshot_turn_at(-1), -1);
    ASSERT_INT_EQ(ghost_snapshot_turn_at(0), -1);
    ASSERT_INT_EQ(ghost_snapshot_turn_at(999), -1);
    ghost_snapshot_free();
    return 0;
}

static int test_gs_restore_empty(void) {
    ghost_snapshot_init("restore_test");
    ASSERT_INT_EQ(ghost_snapshot_restore(1), -1);
    ASSERT_INT_EQ(ghost_snapshot_restore(0), -1);
    ASSERT_INT_EQ(ghost_snapshot_restore_latest(), -1);
    ghost_snapshot_free();
    return 0;
}

static int test_gs_format(void) {
    ghost_snapshot_init("fmt_test");
    char *s = ghost_snapshot_format_list();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Snapshots: 0");
    free(s);
    ghost_snapshot_free();
    return 0;
}

static int test_gs_cleanup_null(void) {
    ghost_snapshot_init(NULL);
    ASSERT_INT_EQ(ghost_snapshot_cleanup(NULL), 0);
    ghost_snapshot_free();
    return 0;
}

static int test_gs_double_free(void) {
    ghost_snapshot_init("dbl_test");
    ghost_snapshot_free();
    ghost_snapshot_free();
    return 0;
}

static int test_gs_not_initialized(void) {
    ghost_snapshot_free();
    ASSERT_INT_EQ(ghost_snapshot_count(), 0);
    ASSERT_FALSE(ghost_snapshot_available());
    return 0;
}

static int test_gs_cleanup_with_id(void) {
    ghost_snapshot_init("cleanup_id_test");
    ASSERT_INT_EQ(ghost_snapshot_cleanup("nonexistent_session"), 0);
    ghost_snapshot_free();
    return 0;
}

static int test_gs_create_restore_cleanup(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-ghost-XXXXXX";
    char note_path[NC_PATH_MAX];
    char contents[128];

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    ghost_snapshot_init("ghost");
    ASSERT_TRUE(ghost_snapshot_available());
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);
    ASSERT_GE(ghost_snapshot_create(1), 0);
    ASSERT_INT_EQ(ghost_snapshot_count(), 1);
    ASSERT_INT_EQ(ghost_snapshot_turn_at(0), 1);

    ASSERT_INT_EQ(th_write_text(note_path, "three\n"), 0);
    ASSERT_INT_EQ(ghost_snapshot_restore(1), 0);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");
    ASSERT_INT_EQ(ghost_snapshot_restore_latest(), 0);
    ASSERT_INT_EQ(ghost_snapshot_cleanup(NULL), 1);
    ghost_snapshot_free();

    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

int test_ghost_snapshot_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("ghost_snapshot");

    RUN_TEST(test_gs_init);
    RUN_TEST(test_gs_zero);
    RUN_TEST(test_gs_turn_bounds);
    RUN_TEST(test_gs_restore_empty);
    RUN_TEST(test_gs_format);
    RUN_TEST(test_gs_cleanup_null);
    RUN_TEST(test_gs_double_free);
    RUN_TEST(test_gs_not_initialized);
    RUN_TEST(test_gs_cleanup_with_id);
    RUN_TEST(test_gs_create_restore_cleanup);

    TEST_RETURN_FAILED();
}
