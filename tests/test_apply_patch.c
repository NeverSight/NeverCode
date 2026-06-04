#include "test.h"
#include "test_helpers.h"
#include "nevercode.h"
#include "tools.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

extern int apply_patch_execute(const cJSON *input, char **output, void *ctx);

static void write_test_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static char *read_test_file(const char *path) {
    return strbuf_read_file_text(path, NULL);
}

static int test_apply_patch_modify(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_ap_test_%d.txt", (int)getpid());
    write_test_file(path, "line1\nline2\nline3\n");

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "--- a/%s\n"
             "+++ b/%s\n"
             "@@ -1,3 +1,3 @@\n"
             " line1\n"
             "-line2\n"
             "+line2_modified\n"
             " line3\n",
             path, path);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "Modified") != NULL);

    char *content = read_test_file(path);
    ASSERT_NOT_NULL(content);
    ASSERT(strstr(content, "line2_modified") != NULL);
    ASSERT(strstr(content, "line2\n") == NULL || strstr(content, "line2_modified") != NULL);

    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

static int test_apply_patch_create(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_ap_new_%d.txt", (int)getpid());
    unlink(path);

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "--- /dev/null\n"
             "+++ b/%s\n"
             "@@ -0,0 +1,3 @@\n"
             "+new line 1\n"
             "+new line 2\n"
             "+new line 3\n",
             path);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Created") != NULL);

    char *content = read_test_file(path);
    ASSERT_NOT_NULL(content);
    ASSERT(strstr(content, "new line 1") != NULL);
    ASSERT(strstr(content, "new line 3") != NULL);

    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

static int test_apply_patch_delete(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_ap_del_%d.txt", (int)getpid());
    write_test_file(path, "to be deleted\n");

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "--- a/%s\n"
             "+++ /dev/null\n"
             "@@ -1,1 +0,0 @@\n"
             "-to be deleted\n",
             path);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Deleted") != NULL);

    struct stat st;
    ASSERT(stat(path, &st) != 0);

    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_apply_patch_missing_param(void) {
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT(strstr(output, "required") != NULL);

    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_apply_patch_multi_hunk(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_ap_mh_%d.txt", (int)getpid());
    write_test_file(path, "a\nb\nc\nd\ne\nf\ng\n");

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "--- a/%s\n"
             "+++ b/%s\n"
             "@@ -1,3 +1,3 @@\n"
             " a\n"
             "-b\n"
             "+B\n"
             " c\n"
             "@@ -5,3 +5,3 @@\n"
             " e\n"
             "-f\n"
             "+F\n"
             " g\n",
             path, path);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "2 hunks") != NULL);

    char *content = read_test_file(path);
    ASSERT_NOT_NULL(content);
    ASSERT(strstr(content, "B\n") != NULL);
    ASSERT(strstr(content, "F\n") != NULL);

    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

static int test_apply_patch_tool_registered(void) {
    const tool_def_t *t = tools_find("ApplyPatch");
    ASSERT_NOT_NULL(t);
    ASSERT(t->destructive == true);
    ASSERT(t->read_only == false);
    ASSERT(t->concurrency_safe == false);
    return 0;
}

static int test_apply_patch_nonexistent_file(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch",
                            "--- a/nonexistent_file_12345.txt\n"
                            "+++ b/nonexistent_file_12345.txt\n"
                            "@@ -1,1 +1,1 @@\n"
                            "-old\n"
                            "+new\n");
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT(strstr(output, "not found") != NULL);

    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_apply_patch_fuzzy_whitespace(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_ap_fuzz_%d.txt", (int)getpid());
    write_test_file(path, "  hello  \n  world  \nfoo\n");

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "--- a/%s\n"
             "+++ b/%s\n"
             "@@ -1,3 +1,3 @@\n"
             " hello\n"
             "-world\n"
             "+WORLD\n"
             " foo\n",
             path, path);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);

    char *content = read_test_file(path);
    ASSERT_NOT_NULL(content);
    ASSERT(strstr(content, "WORLD") != NULL);

    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

static int test_apply_patch_fuzzy_trailing(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_ap_trail_%d.txt", (int)getpid());
    write_test_file(path, "alpha\t\nbeta  \ngamma\n");

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "--- a/%s\n"
             "+++ b/%s\n"
             "@@ -1,3 +1,3 @@\n"
             " alpha\n"
             "-beta\n"
             "+BETA\n"
             " gamma\n",
             path, path);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);

    char *content = read_test_file(path);
    ASSERT_NOT_NULL(content);
    ASSERT(strstr(content, "BETA") != NULL);

    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

/* ── Structured marker format tests ── */

static int test_marker_add_file(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_mk_add_%d.txt", (int)getpid());
    unlink(path);

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "*** Begin Patch\n"
             "*** Add File: %s\n"
             "+first line\n"
             "+second line\n"
             "+third line\n"
             "*** End Patch\n",
             path);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Created") != NULL);

    char *content = read_test_file(path);
    ASSERT_NOT_NULL(content);
    ASSERT(strstr(content, "first line") != NULL);
    ASSERT(strstr(content, "third line") != NULL);

    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

static int test_marker_delete_file(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_mk_del_%d.txt", (int)getpid());
    write_test_file(path, "delete me\n");

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "*** Begin Patch\n"
             "*** Delete File: %s\n"
             "*** End Patch\n",
             path);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Deleted") != NULL);

    struct stat st;
    ASSERT(stat(path, &st) != 0);

    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_marker_update_file(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_mk_upd_%d.txt", (int)getpid());
    write_test_file(path, "alpha\nbeta\ngamma\ndelta\n");

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "*** Begin Patch\n"
             "*** Update File: %s\n"
             " alpha\n"
             "-beta\n"
             "+BETA\n"
             " gamma\n"
             "*** End Patch\n",
             path);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Modified") != NULL);

    char *content = read_test_file(path);
    ASSERT_NOT_NULL(content);
    ASSERT(strstr(content, "BETA") != NULL);
    ASSERT(strstr(content, "alpha") != NULL);
    ASSERT(strstr(content, "gamma") != NULL);

    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

static int test_marker_multi_file(void) {
    char p1[256], p2[256];
    snprintf(p1, sizeof(p1), "/tmp/nevercode_mk_m1_%d.txt", (int)getpid());
    snprintf(p2, sizeof(p2), "/tmp/nevercode_mk_m2_%d.txt", (int)getpid());
    write_test_file(p1, "one\ntwo\nthree\n");
    unlink(p2);

    char patch[4096];
    snprintf(patch, sizeof(patch),
             "*** Begin Patch\n"
             "*** Update File: %s\n"
             " one\n"
             "-two\n"
             "+TWO\n"
             " three\n"
             "*** Add File: %s\n"
             "+new file content\n"
             "*** End Patch\n",
             p1, p2);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);

    char *c1 = read_test_file(p1);
    ASSERT_NOT_NULL(c1);
    ASSERT(strstr(c1, "TWO") != NULL);

    char *c2 = read_test_file(p2);
    ASSERT_NOT_NULL(c2);
    ASSERT(strstr(c2, "new file content") != NULL);

    free(c1);
    free(c2);
    free(output);
    cJSON_Delete(input);
    unlink(p1);
    unlink(p2);
    return 0;
}

static int test_marker_eof_hint(void) {
    char p1[256];
    snprintf(p1, sizeof(p1), "/tmp/nevercode_ap_eof_%d.txt", (int)getpid());
    write_test_file(p1, "alpha\nbeta\ngamma\ndelta\n");

    char patch[2048];
    snprintf(patch, sizeof(patch),
             "*** Begin Patch\n"
             "*** Update File: %s\n"
             "*** End of File\n"
             " gamma\n"
             "-delta\n"
             "+delta_modified\n"
             "*** End Patch\n",
             p1);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);

    char *c = read_test_file(p1);
    ASSERT_NOT_NULL(c);
    ASSERT(strstr(c, "delta_modified") != NULL);
    ASSERT(strstr(c, "alpha") != NULL);

    free(c);
    free(output);
    cJSON_Delete(input);
    unlink(p1);
    return 0;
}

static int test_marker_lenient_whitespace(void) {
    char p1[256];
    snprintf(p1, sizeof(p1), "/tmp/nevercode_ap_lenient_%d.txt", (int)getpid());
    write_test_file(p1, "aaa\nbbb\nccc\n");

    /* markers have leading whitespace — should still be recognized */
    char patch[2048];
    snprintf(patch, sizeof(patch),
             "  *** Begin Patch\n"
             "  *** Update File: %s\n"
             " bbb\n"
             "-ccc\n"
             "+ccc_new\n"
             "  *** End Patch\n",
             p1);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    int rc = apply_patch_execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);

    char *c = read_test_file(p1);
    ASSERT_NOT_NULL(c);
    ASSERT(strstr(c, "ccc_new") != NULL);

    free(c);
    free(output);
    cJSON_Delete(input);
    unlink(p1);
    return 0;
}

static int test_apply_patch_restore_unified_create_delete(void) {
    char tmpdir[] = "/tmp/nevercode-ap-XXXXXX";
    char deleted_path[NC_PATH_MAX];
    char added_path[NC_PATH_MAX];
    char added_dir[NC_PATH_MAX];
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(deleted_path, sizeof(deleted_path), "%s/old/deleted.txt", tmpdir);
    snprintf(added_path, sizeof(added_path), "%s/new/deep/added.txt", tmpdir);
    snprintf(added_dir, sizeof(added_dir), "%s/new", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/old", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    write_test_file(deleted_path, "gone soon\n");

    char patch[4096];
    snprintf(patch, sizeof(patch),
             "diff --git a/%s b/%s\n"
             "deleted file mode 100644\n"
             "--- a/%s\n"
             "+++ /dev/null\n"
             "@@ -1,1 +0,0 @@\n"
             "-gone soon\n"
             "diff --git a/%s b/%s\n"
             "new file mode 100644\n"
             "--- /dev/null\n"
             "+++ b/%s\n"
             "@@ -0,0 +1,1 @@\n"
             "+brand new\n",
             deleted_path, deleted_path, deleted_path, added_path, added_path, added_path);

    file_history_init();
    file_history_next_turn();
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    ASSERT_INT_EQ(apply_patch_execute(input, &output, NULL), 0);
    ASSERT(strstr(output, "Deleted") != NULL);
    ASSERT(strstr(output, "Created") != NULL);
    ASSERT_FALSE(access(deleted_path, F_OK) == 0);
    ASSERT_TRUE(access(added_path, F_OK) == 0);

    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 2);
    ASSERT_INT_EQ(restored, 2);
    ASSERT_INT_EQ(errors, 0);
    ASSERT_TRUE(access(deleted_path, F_OK) == 0);
    ASSERT_FALSE(access(added_path, F_OK) == 0);
    ASSERT_FALSE(access(added_dir, F_OK) == 0);

    free(output);
    cJSON_Delete(input);
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

static int test_apply_patch_restore_marker_move_add_delete(void) {
    char tmpdir[] = "/tmp/nevercode-ap-XXXXXX";
    char source_path[NC_PATH_MAX];
    char target_path[NC_PATH_MAX];
    char deleted_path[NC_PATH_MAX];
    char added_path[NC_PATH_MAX];
    char added_root[NC_PATH_MAX];
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(source_path, sizeof(source_path), "%s/src/from.txt", tmpdir);
    snprintf(target_path, sizeof(target_path), "%s/dest/existing.txt", tmpdir);
    snprintf(deleted_path, sizeof(deleted_path), "%s/trash/gone.txt", tmpdir);
    snprintf(added_path, sizeof(added_path), "%s/scratch/deep/new.txt", tmpdir);
    snprintf(added_root, sizeof(added_root), "%s/scratch", tmpdir);
    {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/src", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
        snprintf(parent, sizeof(parent), "%s/dest", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
        snprintf(parent, sizeof(parent), "%s/trash", tmpdir);
        ASSERT_INT_EQ(nc_mkdir_p(parent), 0);
    }
    write_test_file(source_path, "alpha\nbeta\ngamma\n");
    write_test_file(target_path, "target old\n");
    write_test_file(deleted_path, "restore me\n");

    char patch[4096];
    snprintf(patch, sizeof(patch),
             "*** Begin Patch\n"
             "*** Update File: %s\n"
             "*** Move to: %s\n"
             " alpha\n"
             "-beta\n"
             "+BETA\n"
             " gamma\n"
             "*** Delete File: %s\n"
             "*** Add File: %s\n"
             "+draft line\n"
             "*** End Patch\n",
             source_path, target_path, deleted_path, added_path);

    file_history_init();
    file_history_next_turn();
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "patch", patch);
    char *output = NULL;
    ASSERT_INT_EQ(apply_patch_execute(input, &output, NULL), 0);
    ASSERT(strstr(output, target_path) != NULL);
    ASSERT_FALSE(access(source_path, F_OK) == 0);
    ASSERT_FALSE(access(deleted_path, F_OK) == 0);
    ASSERT_TRUE(access(added_path, F_OK) == 0);
    {
        char *target = read_test_file(target_path);
        ASSERT_NOT_NULL(target);
        ASSERT(strstr(target, "BETA") != NULL);
        free(target);
    }

    int restored = 0, errors = 0;
    ASSERT_INT_EQ(file_history_restore_turn(1, &restored, &errors), 4);
    ASSERT_INT_EQ(restored, 4);
    ASSERT_INT_EQ(errors, 0);
    {
        char *source = read_test_file(source_path);
        char *target = read_test_file(target_path);
        char *deleted = read_test_file(deleted_path);
        ASSERT_NOT_NULL(source);
        ASSERT_NOT_NULL(target);
        ASSERT_NOT_NULL(deleted);
        ASSERT(strstr(source, "beta") != NULL);
        ASSERT(strstr(target, "target old") != NULL);
        ASSERT(strstr(deleted, "restore me") != NULL);
        free(source);
        free(target);
        free(deleted);
    }
    ASSERT_FALSE(access(added_path, F_OK) == 0);
    ASSERT_FALSE(access(added_root, F_OK) == 0);

    free(output);
    cJSON_Delete(input);
    file_history_free();
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

int test_apply_patch_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("apply_patch");

    RUN_TEST(test_apply_patch_modify);
    RUN_TEST(test_apply_patch_create);
    RUN_TEST(test_apply_patch_delete);
    RUN_TEST(test_apply_patch_missing_param);
    RUN_TEST(test_apply_patch_multi_hunk);
    RUN_TEST(test_apply_patch_tool_registered);
    RUN_TEST(test_apply_patch_nonexistent_file);
    RUN_TEST(test_apply_patch_fuzzy_whitespace);
    RUN_TEST(test_apply_patch_fuzzy_trailing);
    RUN_TEST(test_marker_add_file);
    RUN_TEST(test_marker_delete_file);
    RUN_TEST(test_marker_update_file);
    RUN_TEST(test_marker_multi_file);
    RUN_TEST(test_marker_eof_hint);
    RUN_TEST(test_marker_lenient_whitespace);
    RUN_TEST(test_apply_patch_restore_unified_create_delete);
    RUN_TEST(test_apply_patch_restore_marker_move_add_delete);

    TEST_RETURN_FAILED();
}
