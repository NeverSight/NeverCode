/*
 * test_turn_diff_baseline.c — Tests for enhanced turn_diff with
 * baseline snapshots, unified diff output, rename tracking,
 * binary detection, context lines, and file mode awareness.
 */
#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char g_tmpdir[256];

static void setup_tmpdir(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/nc_test_turndiff_XXXXXX");
    mkdtemp(g_tmpdir);
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static void write_binary(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, len, f);
        fclose(f);
    }
}

static int test_baseline_capture(void) {
    setup_tmpdir();
    turn_diff_init();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/test.c", g_tmpdir);
    write_file(filepath, "int main() { return 0; }\n");

    turn_diff_snapshot_baseline(filepath, 1);
    ASSERT_INT_EQ(turn_diff_baseline_count(), 1);

    turn_diff_snapshot_baseline(filepath, 2);
    ASSERT_INT_EQ(turn_diff_baseline_count(), 1);

    turn_diff_free();
    unlink(filepath);
    rmdir(g_tmpdir);
    return 0;
}

static int test_unified_no_change(void) {
    setup_tmpdir();
    turn_diff_init();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/noop.c", g_tmpdir);
    write_file(filepath, "line1\nline2\nline3\n");

    turn_diff_snapshot_baseline(filepath, 1);
    char *diff = turn_diff_unified(filepath);
    ASSERT_NOT_NULL(diff);
    ASSERT_STR_EQ(diff, "");
    free(diff);

    turn_diff_free();
    unlink(filepath);
    rmdir(g_tmpdir);
    return 0;
}

static int test_unified_with_changes(void) {
    setup_tmpdir();
    turn_diff_init();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/edit.c", g_tmpdir);
    write_file(filepath, "line1\nline2\nline3\n");

    turn_diff_snapshot_baseline(filepath, 1);
    write_file(filepath, "line1\nline2_modified\nline3\nline4\n");

    char *diff = turn_diff_unified(filepath);
    ASSERT_NOT_NULL(diff);
    ASSERT(strlen(diff) > 0);
    ASSERT(strstr(diff, "diff --git") != NULL);
    ASSERT(strstr(diff, "--- a/") != NULL);
    ASSERT(strstr(diff, "+++ b/") != NULL);
    ASSERT(strstr(diff, "-line2") != NULL);
    ASSERT(strstr(diff, "+line2_modified") != NULL);
    ASSERT(strstr(diff, "+line4") != NULL);
    free(diff);

    turn_diff_free();
    unlink(filepath);
    rmdir(g_tmpdir);
    return 0;
}

static int test_unified_new_file(void) {
    setup_tmpdir();
    turn_diff_init();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/new.c", g_tmpdir);

    turn_diff_snapshot_baseline(filepath, 1);
    write_file(filepath, "hello world\n");

    char *diff = turn_diff_unified(filepath);
    ASSERT_NOT_NULL(diff);
    ASSERT(strstr(diff, "new file mode") != NULL);
    ASSERT(strstr(diff, "/dev/null") != NULL);
    ASSERT(strstr(diff, "+hello world") != NULL);
    free(diff);

    turn_diff_free();
    unlink(filepath);
    rmdir(g_tmpdir);
    return 0;
}

static int test_unified_deleted_file(void) {
    setup_tmpdir();
    turn_diff_init();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/del.c", g_tmpdir);
    write_file(filepath, "old content\n");

    turn_diff_snapshot_baseline(filepath, 1);
    unlink(filepath);

    char *diff = turn_diff_unified(filepath);
    ASSERT_NOT_NULL(diff);
    ASSERT(strstr(diff, "deleted file mode") != NULL);
    ASSERT(strstr(diff, "/dev/null") != NULL);
    ASSERT(strstr(diff, "-old content") != NULL);
    free(diff);

    turn_diff_free();
    rmdir(g_tmpdir);
    return 0;
}

static int test_unified_all(void) {
    setup_tmpdir();
    turn_diff_init();

    char f1[512], f2[512];
    snprintf(f1, sizeof(f1), "%s/a.c", g_tmpdir);
    snprintf(f2, sizeof(f2), "%s/b.c", g_tmpdir);
    write_file(f1, "aaa\n");
    write_file(f2, "bbb\n");

    turn_diff_snapshot_baseline(f1, 1);
    turn_diff_snapshot_baseline(f2, 1);
    write_file(f1, "aaa_changed\n");

    char *all = turn_diff_unified_all();
    ASSERT_NOT_NULL(all);
    ASSERT(strstr(all, "a.c") != NULL);
    free(all);

    turn_diff_free();
    unlink(f1);
    unlink(f2);
    rmdir(g_tmpdir);
    return 0;
}

static int test_files_changed_count(void) {
    setup_tmpdir();
    turn_diff_init();

    char f1[512], f2[512];
    snprintf(f1, sizeof(f1), "%s/x.c", g_tmpdir);
    snprintf(f2, sizeof(f2), "%s/y.c", g_tmpdir);
    write_file(f1, "x\n");
    write_file(f2, "y\n");

    turn_diff_snapshot_baseline(f1, 1);
    turn_diff_snapshot_baseline(f2, 1);

    ASSERT_INT_EQ(turn_diff_files_changed(), 0);

    write_file(f1, "x_modified\n");
    ASSERT_INT_EQ(turn_diff_files_changed(), 1);

    write_file(f2, "y_modified\n");
    ASSERT_INT_EQ(turn_diff_files_changed(), 2);

    turn_diff_free();
    unlink(f1);
    unlink(f2);
    rmdir(g_tmpdir);
    return 0;
}

static int test_record_auto_baselines(void) {
    setup_tmpdir();
    turn_diff_init();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/auto.c", g_tmpdir);
    write_file(filepath, "original\n");

    ASSERT_INT_EQ(turn_diff_baseline_count(), 0);
    turn_diff_record(1, filepath, 5, 0);
    ASSERT_INT_EQ(turn_diff_baseline_count(), 1);
    ASSERT_INT_EQ(turn_diff_total_added(), 5);

    turn_diff_free();
    unlink(filepath);
    rmdir(g_tmpdir);
    return 0;
}

static int test_context_lines(void) {
    setup_tmpdir();
    turn_diff_init();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/ctx.c", g_tmpdir);
    write_file(filepath, "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n");

    turn_diff_snapshot_baseline(filepath, 1);
    write_file(filepath, "a\nb\nc\nd\nMODIFIED\nf\ng\nh\ni\nj\n");

    char *diff = turn_diff_unified(filepath);
    ASSERT_NOT_NULL(diff);
    ASSERT(strstr(diff, " b") != NULL || strstr(diff, " c") != NULL || strstr(diff, " d") != NULL);
    ASSERT(strstr(diff, "-e") != NULL);
    ASSERT(strstr(diff, "+MODIFIED") != NULL);
    free(diff);

    turn_diff_free();
    unlink(filepath);
    rmdir(g_tmpdir);
    return 0;
}

static int test_rename_tracking(void) {
    setup_tmpdir();
    turn_diff_init();

    char old_fp[512], new_fp[512];
    snprintf(old_fp, sizeof(old_fp), "%s/old_name.c", g_tmpdir);
    snprintf(new_fp, sizeof(new_fp), "%s/new_name.c", g_tmpdir);
    write_file(old_fp, "content\n");

    turn_diff_snapshot_baseline(old_fp, 1);
    rename(old_fp, new_fp);
    turn_diff_track_rename(old_fp, new_fp);
    write_file(new_fp, "content_modified\n");

    char *diff = turn_diff_unified(new_fp);
    ASSERT_NOT_NULL(diff);
    ASSERT(strstr(diff, "rename from") != NULL);
    ASSERT(strstr(diff, "rename to") != NULL);
    ASSERT(strstr(diff, "old_name.c") != NULL);
    ASSERT(strstr(diff, "new_name.c") != NULL);
    free(diff);

    turn_diff_free();
    unlink(new_fp);
    rmdir(g_tmpdir);
    return 0;
}

static int test_binary_detection(void) {
    setup_tmpdir();
    turn_diff_init();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/bin.dat", g_tmpdir);
    char data[] = "HEADER\x00\x01\x02\x03TAIL";
    write_binary(filepath, data, sizeof(data) - 1);

    turn_diff_snapshot_baseline(filepath, 1);
    char data2[] = "HEADER\x00\x01\x02\x03\x04CHANGED";
    write_binary(filepath, data2, sizeof(data2) - 1);

    char *diff = turn_diff_unified(filepath);
    ASSERT_NOT_NULL(diff);
    ASSERT(strstr(diff, "Binary files differ") != NULL);
    free(diff);

    turn_diff_free();
    unlink(filepath);
    rmdir(g_tmpdir);
    return 0;
}

static int test_git_diff_header(void) {
    setup_tmpdir();
    turn_diff_init();

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/hdr.c", g_tmpdir);
    write_file(filepath, "old\n");

    turn_diff_snapshot_baseline(filepath, 1);
    write_file(filepath, "new\n");

    char *diff = turn_diff_unified(filepath);
    ASSERT_NOT_NULL(diff);
    ASSERT(strstr(diff, "diff --git a/") != NULL);
    ASSERT(strstr(diff, " b/") != NULL);
    free(diff);

    turn_diff_free();
    unlink(filepath);
    rmdir(g_tmpdir);
    return 0;
}

int test_turn_diff_baseline_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("turn_diff_baseline");
    RUN_TEST(test_baseline_capture);
    RUN_TEST(test_unified_no_change);
    RUN_TEST(test_unified_with_changes);
    RUN_TEST(test_unified_new_file);
    RUN_TEST(test_unified_deleted_file);
    RUN_TEST(test_unified_all);
    RUN_TEST(test_files_changed_count);
    RUN_TEST(test_record_auto_baselines);
    RUN_TEST(test_context_lines);
    RUN_TEST(test_rename_tracking);
    RUN_TEST(test_binary_detection);
    RUN_TEST(test_git_diff_header);
    TEST_RETURN_FAILED();
}
