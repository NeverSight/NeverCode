/*
 * test_node_artifact.c — Per-node artifact storage tests.
 */

#include "test.h"
#include "harness.h"
#include <sys/stat.h>
#include <unistd.h>

static char g_tmpdir[NC_PATH_MAX];

static void setup_tmpdir(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/nc_art_test_%d", (int)getpid());
    nc_mkdir_p(g_tmpdir);
}

static void cleanup_tmpdir(void) {
    char cmd[NC_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmpdir);
    (void)system(cmd);
}

static int t_init_and_free(void) {
    artifact_store_t as;
    artifact_store_init(&as, "/tmp/nonexistent");
    ASSERT_STR_EQ(as.root_dir, "/tmp/nonexistent");
    ASSERT_INT_EQ(as.nnodes, 0);
    artifact_store_free(&as);
    ASSERT_INT_EQ(as.nnodes, 0);
    return 0;
}

static int t_ensure_node_creates_dir(void) {
    setup_tmpdir();
    artifact_store_t as;
    artifact_store_init(&as, g_tmpdir);

    ASSERT_INT_EQ(artifact_store_ensure_node(&as, "build"), 0);
    ASSERT_INT_EQ(as.nnodes, 1);

    char node_dir[NC_PATH_MAX];
    snprintf(node_dir, sizeof(node_dir), "%s/build", g_tmpdir);
    struct stat st;
    ASSERT(stat(node_dir, &st) == 0 && S_ISDIR(st.st_mode));

    ASSERT_INT_EQ(artifact_store_ensure_node(&as, "build"), 0);
    ASSERT_INT_EQ(as.nnodes, 1);

    artifact_store_free(&as);
    cleanup_tmpdir();
    return 0;
}

static int t_write_and_read(void) {
    setup_tmpdir();
    artifact_store_t as;
    artifact_store_init(&as, g_tmpdir);

    int rc = artifact_write(&as, "step1", "output.txt", "hello world");
    ASSERT(rc > 0);
    ASSERT_INT_EQ(as.nnodes, 1);
    ASSERT_INT_EQ(artifact_slot_count(&as, "step1"), 1);

    char *content = artifact_read(&as, "step1", "output.txt");
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "hello world");
    free(content);

    artifact_store_free(&as);
    cleanup_tmpdir();
    return 0;
}

static int t_append_accumulates(void) {
    setup_tmpdir();
    artifact_store_t as;
    artifact_store_init(&as, g_tmpdir);

    artifact_append(&as, "log_node", "stderr.log", "line 1\n");
    artifact_append(&as, "log_node", "stderr.log", "line 2\n");

    char *content = artifact_read(&as, "log_node", "stderr.log");
    ASSERT_NOT_NULL(content);
    ASSERT_STR_CONTAINS(content, "line 1");
    ASSERT_STR_CONTAINS(content, "line 2");
    free(content);

    artifact_store_free(&as);
    cleanup_tmpdir();
    return 0;
}

static int t_write_overwrites(void) {
    setup_tmpdir();
    artifact_store_t as;
    artifact_store_init(&as, g_tmpdir);

    artifact_write(&as, "n1", "result.txt", "old");
    artifact_write(&as, "n1", "result.txt", "new");

    char *content = artifact_read(&as, "n1", "result.txt");
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "new");
    free(content);

    artifact_store_free(&as);
    cleanup_tmpdir();
    return 0;
}

static int t_path_lookup(void) {
    setup_tmpdir();
    artifact_store_t as;
    artifact_store_init(&as, g_tmpdir);

    artifact_write(&as, "n2", "trace.json", "{}");
    const char *path = artifact_path(&as, "n2", "trace.json");
    ASSERT_NOT_NULL(path);
    ASSERT_STR_CONTAINS(path, "trace.json");
    ASSERT_STR_CONTAINS(path, g_tmpdir);

    ASSERT_NULL(artifact_path(&as, "n2", "missing"));
    ASSERT_NULL(artifact_path(&as, "nonode", "trace.json"));

    artifact_store_free(&as);
    cleanup_tmpdir();
    return 0;
}

static int t_attempt_naming(void) {
    setup_tmpdir();
    artifact_store_t as;
    artifact_store_init(&as, g_tmpdir);

    artifact_write_attempt(&as, "retry", "output.txt", 1, "first");
    artifact_write_attempt(&as, "retry", "output.txt", 2, "second");

    ASSERT_INT_EQ(artifact_slot_count(&as, "retry"), 2);

    const char *p1 = artifact_path(&as, "retry", "output-attempt-1.txt");
    const char *p2 = artifact_path(&as, "retry", "output-attempt-2.txt");
    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);

    artifact_store_free(&as);
    cleanup_tmpdir();
    return 0;
}

static int t_rebuild_from_disk(void) {
    setup_tmpdir();

    {
        artifact_store_t as;
        artifact_store_init(&as, g_tmpdir);
        artifact_write(&as, "alpha", "data.bin", "binary-ish");
        artifact_write(&as, "beta", "log.txt", "some log");
        artifact_store_free(&as);
    }

    artifact_store_t as2;
    artifact_store_init(&as2, g_tmpdir);
    ASSERT_INT_EQ(artifact_store_rebuild(&as2), 0);
    ASSERT_GE(as2.nnodes, 2);

    char *data = artifact_read(&as2, "alpha", "data.bin");
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ(data, "binary-ish");
    free(data);

    artifact_store_free(&as2);
    cleanup_tmpdir();
    return 0;
}

static int t_manifest_format(void) {
    setup_tmpdir();
    artifact_store_t as;
    artifact_store_init(&as, g_tmpdir);

    artifact_write(&as, "build", "stdout", "output here");
    artifact_write(&as, "build", "stderr", "err here");

    char *manifest = artifact_format_manifest(&as, "build");
    ASSERT_NOT_NULL(manifest);
    ASSERT_STR_CONTAINS(manifest, "build");
    ASSERT_STR_CONTAINS(manifest, "2 files");
    ASSERT_STR_CONTAINS(manifest, "stdout");
    ASSERT_STR_CONTAINS(manifest, "stderr");
    free(manifest);

    ASSERT_NULL(artifact_format_manifest(&as, "missing"));

    artifact_store_free(&as);
    cleanup_tmpdir();
    return 0;
}

static int t_null_safety(void) {
    artifact_store_t as;
    artifact_store_init(&as, g_tmpdir);

    ASSERT(artifact_store_ensure_node(&as, NULL) < 0);
    ASSERT(artifact_write(&as, NULL, "a", "b") < 0);
    ASSERT(artifact_write(&as, "n", NULL, "b") < 0);
    ASSERT(artifact_append(&as, NULL, "a", "b") < 0);
    ASSERT_NULL(artifact_read(&as, "missing", "x"));
    ASSERT_NULL(artifact_path(&as, "missing", "x"));
    ASSERT_INT_EQ(artifact_slot_count(&as, "missing"), 0);
    ASSERT(artifact_write_attempt(&as, "n", NULL, 1, "x") < 0);
    ASSERT(artifact_write_attempt(&as, "n", "x", 0, "x") < 0);

    artifact_store_free(&as);
    return 0;
}

int test_node_artifact_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("node_artifact");

    RUN_TEST(t_init_and_free);
    RUN_TEST(t_ensure_node_creates_dir);
    RUN_TEST(t_write_and_read);
    RUN_TEST(t_append_accumulates);
    RUN_TEST(t_write_overwrites);
    RUN_TEST(t_path_lookup);
    RUN_TEST(t_attempt_naming);
    RUN_TEST(t_rebuild_from_disk);
    RUN_TEST(t_manifest_format);
    RUN_TEST(t_null_safety);

    TEST_RETURN_FAILED();
}
