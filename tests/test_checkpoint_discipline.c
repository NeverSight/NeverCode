#include "test.h"
#include "test_helpers.h"
#include "harness.h"
#include "defaults.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int test_cp_init(void) {
    checkpoint_discipline_init("s", 5);
    ASSERT_INT_EQ(checkpoint_discipline_count(), 0);
    checkpoint_discipline_free();
    return 0;
}
static int test_cp_default(void) {
    checkpoint_discipline_init("s", 0);
    ASSERT_INT_EQ(checkpoint_discipline_count(), 0);
    checkpoint_discipline_free();
    return 0;
}
static int test_cp_not_ready(void) {
    checkpoint_discipline_init("s", 5);
    ASSERT_FALSE(checkpoint_discipline_should_fire(1, true));
    ASSERT_FALSE(checkpoint_discipline_should_fire(2, true));
    checkpoint_discipline_free();
    return 0;
}
static int test_cp_no_changes(void) {
    int i;
    checkpoint_discipline_init("s", 3);
    for (i = 0; i < 10; i++)
        ASSERT_FALSE(checkpoint_discipline_should_fire(i, false));
    checkpoint_discipline_free();
    return 0;
}
static int test_cp_format(void) {
    checkpoint_discipline_init("f", 10);
    char *s = checkpoint_discipline_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Checkpoints: 0");
    free(s);
    checkpoint_discipline_free();
    return 0;
}
static int test_cp_null(void) {
    checkpoint_discipline_init(NULL, 5);
    char *s = checkpoint_discipline_format();
    ASSERT_NOT_NULL(s);
    free(s);
    checkpoint_discipline_free();
    return 0;
}
static int test_cp_threshold(void) {
    checkpoint_discipline_init("s", 3);
    checkpoint_discipline_should_fire(1, true);
    checkpoint_discipline_should_fire(2, true);
    ASSERT_TRUE(checkpoint_discipline_should_fire(3, true));
    checkpoint_discipline_free();
    return 0;
}
static int test_cp_exclude_count(void) {
    int n = checkpoint_exclude_count();
    ASSERT(n >= 20);
    const char *first = checkpoint_exclude_pattern(0);
    ASSERT_NOT_NULL(first);
    ASSERT(checkpoint_exclude_pattern(-1) == NULL);
    ASSERT(checkpoint_exclude_pattern(n) == NULL);
    return 0;
}
static int test_cp_format_excludes(void) {
    checkpoint_discipline_init("ex", 5);
    char *s = checkpoint_discipline_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "excludes:");
    free(s);
    checkpoint_discipline_free();
    return 0;
}

static int test_cp_create_restore_cleanup(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-checkpoint-XXXXXX";
    char note_path[NC_PATH_MAX];
    char contents[128];

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);

    checkpoint_discipline_init("cp", 1);
    ASSERT_TRUE(checkpoint_discipline_should_fire(1, true));
    ASSERT_INT_EQ(checkpoint_discipline_create(1), 0);
    ASSERT_INT_EQ(checkpoint_discipline_count(), 1);

    ASSERT_INT_EQ(th_write_text(note_path, "three\n"), 0);
    ASSERT_INT_EQ(checkpoint_discipline_restore(1), 0);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");

    ASSERT_INT_EQ(checkpoint_discipline_cleanup(), 1);
    ASSERT_INT_EQ(checkpoint_discipline_count(), 0);
    checkpoint_discipline_free();

    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_cp_snapshot_restore_state(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-checkpoint-snap-XXXXXX";
    char note_path[NC_PATH_MAX];
    char contents[128];

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);

    checkpoint_discipline_init("snap", 2);
    ASSERT_FALSE(checkpoint_discipline_should_fire(1, true));
    ASSERT_TRUE(checkpoint_discipline_should_fire(2, true));
    ASSERT_INT_EQ(checkpoint_discipline_create(2), 0);

    checkpoint_discipline_snapshot_t snap;
    checkpoint_discipline_snapshot(&snap);

    checkpoint_discipline_init("reset", 10);
    checkpoint_discipline_restore_state(&snap);
    ASSERT_INT_EQ(checkpoint_discipline_count(), 1);

    char *s = checkpoint_discipline_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "turn 2");
    ASSERT_STR_CONTAINS(s, "snap");
    free(s);

    ASSERT_FALSE(checkpoint_discipline_should_fire(3, true));
    ASSERT_INT_EQ(th_write_text(note_path, "three\n"), 0);
    ASSERT_INT_EQ(checkpoint_discipline_restore(2), 0);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");

    ASSERT_INT_EQ(checkpoint_discipline_cleanup(), 1);
    checkpoint_discipline_free();
    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_cp_restore_state_null(void) {
    checkpoint_discipline_init("s", 5);
    checkpoint_discipline_should_fire(1, true);
    checkpoint_discipline_restore_state(NULL);
    ASSERT_INT_EQ(checkpoint_discipline_count(), 0);
    checkpoint_discipline_free();
    return 0;
}

int test_checkpoint_discipline_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("checkpoint_discipline");
    RUN_TEST(test_cp_init);
    RUN_TEST(test_cp_default);
    RUN_TEST(test_cp_not_ready);
    RUN_TEST(test_cp_no_changes);
    RUN_TEST(test_cp_format);
    RUN_TEST(test_cp_null);
    RUN_TEST(test_cp_threshold);
    RUN_TEST(test_cp_exclude_count);
    RUN_TEST(test_cp_format_excludes);
    RUN_TEST(test_cp_create_restore_cleanup);
    RUN_TEST(test_cp_snapshot_restore_state);
    RUN_TEST(test_cp_restore_state_null);
    TEST_RETURN_FAILED();
}
