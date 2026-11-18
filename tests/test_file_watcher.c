#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

static int write_text_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -1;
    size_t len = strlen(text);
    int rc = fwrite(text, 1, len, fp) == len ? 0 : -1;
    fclose(fp);
    return rc;
}

static bool has_watch_path(file_watcher_t *w, const char *path) {
    for (int i = 0; i < file_watcher_count(w); i++) {
        const char *candidate = file_watcher_path(w, i);
        if (candidate && strcmp(candidate, path) == 0)
            return true;
    }
    return false;
}

static int test_init_free(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    ASSERT_INT_EQ(w.num_watches, 0);
    ASSERT_INT_EQ(w.num_callbacks, 0);
    ASSERT(w.kq_fd == -1);
    ASSERT(!w.running);
    file_watcher_free(&w);
    return 0;
}

static int test_add_nonexistent(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    int rc = file_watcher_add(&w, "/tmp/__nonexistent_path_12345__", WATCH_FILE);
    ASSERT(rc < 0);
    ASSERT_INT_EQ(file_watcher_count(&w), 0);
    file_watcher_free(&w);
    return 0;
}

static int test_add_real_file(void) {
    char tmpf[] = "/tmp/nevercode_fw_test_XXXXXX";
    int fd = mkstemp(tmpf);
    ASSERT(fd >= 0);
    ASSERT_INT_EQ(write(fd, "test", 4), 4);
    close(fd);

    file_watcher_t w;
    file_watcher_init(&w);
    int idx = file_watcher_add(&w, tmpf, WATCH_FILE);
    ASSERT(idx >= 0);
    ASSERT_INT_EQ(file_watcher_count(&w), 1);
    ASSERT_STR_EQ(file_watcher_path(&w, 0), tmpf);

    file_watcher_free(&w);
    unlink(tmpf);
    return 0;
}

static int test_add_directory(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    int idx = file_watcher_add(&w, "/tmp", WATCH_DIR);
    ASSERT(idx >= 0);
    ASSERT_INT_EQ(file_watcher_count(&w), 1);
    file_watcher_free(&w);
    return 0;
}

static int test_add_duplicate(void) {
    char tmpf[] = "/tmp/nevercode_fw_dup_XXXXXX";
    int fd = mkstemp(tmpf);
    ASSERT(fd >= 0);
    close(fd);

    file_watcher_t w;
    file_watcher_init(&w);
    int i1 = file_watcher_add(&w, tmpf, WATCH_FILE);
    int i2 = file_watcher_add(&w, tmpf, WATCH_FILE);
    ASSERT(i1 >= 0);
    ASSERT_INT_EQ(i1, i2);
    ASSERT_INT_EQ(file_watcher_count(&w), 1);
    file_watcher_free(&w);
    unlink(tmpf);
    return 0;
}

static int test_wrong_type(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    int rc = file_watcher_add(&w, "/tmp", WATCH_FILE);
    ASSERT(rc < 0);
    file_watcher_free(&w);
    return 0;
}

static int test_add_null(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    ASSERT(file_watcher_add(&w, NULL, WATCH_FILE) < 0);
    file_watcher_free(&w);
    return 0;
}

static int cb_count = 0;
static void test_callback(const char *path, file_watch_type_t type, void *ctx) {
    (void)path;
    (void)type;
    (void)ctx;
    cb_count++;
}

static int test_callback_registration(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    int rc = file_watcher_add_callback(&w, test_callback, NULL);
    ASSERT(rc >= 0);
    ASSERT_INT_EQ(w.num_callbacks, 1);
    rc = file_watcher_add_callback(&w, NULL, NULL);
    ASSERT(rc < 0);
    file_watcher_free(&w);
    return 0;
}

static int test_poll_check(void) {
    char tmpf[] = "/tmp/nevercode_fw_poll_XXXXXX";
    int fd = mkstemp(tmpf);
    ASSERT(fd >= 0);
    ASSERT_INT_EQ(write(fd, "a", 1), 1);
    close(fd);

    file_watcher_t w;
    file_watcher_init(&w);
    cb_count = 0;
    file_watcher_add(&w, tmpf, WATCH_FILE);
    file_watcher_add_callback(&w, test_callback, NULL);

    int changed = file_watcher_check_poll(&w);
    ASSERT_INT_EQ(changed, 0);

    usleep(1100000);
    fd = open(tmpf, 1);
    if (fd >= 0) {
        ASSERT_INT_EQ(write(fd, "bb", 2), 2);
        close(fd);
    }

    changed = file_watcher_check_poll(&w);
    ASSERT(changed >= 0);

    file_watcher_free(&w);
    unlink(tmpf);
    return 0;
}

static int test_path_out_of_range(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    ASSERT_NULL(file_watcher_path(&w, -1));
    ASSERT_NULL(file_watcher_path(&w, 0));
    ASSERT_NULL(file_watcher_path(&w, 99));
    file_watcher_free(&w);
    return 0;
}

static int test_active_state(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    ASSERT(!file_watcher_active(&w));
    file_watcher_free(&w);
    return 0;
}

static int test_start_empty(void) {
    file_watcher_t w;
    file_watcher_init(&w);
    ASSERT(file_watcher_start(&w) < 0);
    file_watcher_free(&w);
    return 0;
}

static int test_global(void) {
    file_watcher_t *g = file_watcher_global();
    ASSERT_NOT_NULL(g);
    file_watcher_t *g2 = file_watcher_global();
    ASSERT(g == g2);
    return 0;
}

static int test_add_config_paths_project_doc_variants(void) {
    char tmpdir[] = "/tmp/nevercode_fw_cfg_XXXXXX";
    char root_override[NC_PATH_MAX];
    char compat_local[NC_PATH_MAX];
    char dot_agents_dir[NC_PATH_MAX];
    char dot_agents_local[NC_PATH_MAX];
    char dot_claude_dir[NC_PATH_MAX];
    char dot_claude_base[NC_PATH_MAX];

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    snprintf(root_override, sizeof(root_override), "%s/AGENTS.override.md", tmpdir);
    snprintf(compat_local, sizeof(compat_local), "%s/claude.local.md", tmpdir);
    snprintf(dot_agents_dir, sizeof(dot_agents_dir), "%s/.agents", tmpdir);
    snprintf(dot_agents_local, sizeof(dot_agents_local), "%s/.agents/AGENTS.local.md", tmpdir);
    snprintf(dot_claude_dir, sizeof(dot_claude_dir), "%s/.claude", tmpdir);
    snprintf(dot_claude_base, sizeof(dot_claude_base), "%s/.claude/claude.md", tmpdir);

    ASSERT_INT_EQ(write_text_file(root_override, "root override\n"), 0);
    ASSERT_INT_EQ(write_text_file(compat_local, "compat local\n"), 0);
    ASSERT_INT_EQ(mkdir(dot_agents_dir, 0755), 0);
    ASSERT_INT_EQ(write_text_file(dot_agents_local, "dot agents local\n"), 0);
    ASSERT_INT_EQ(mkdir(dot_claude_dir, 0755), 0);
    ASSERT_INT_EQ(write_text_file(dot_claude_base, "dot claude base\n"), 0);

    file_watcher_t w;
    file_watcher_init(&w);
    int added = file_watcher_add_config_paths(&w, tmpdir);
    ASSERT_GE(added, 4);
    ASSERT_TRUE(has_watch_path(&w, root_override));
    ASSERT_TRUE(has_watch_path(&w, compat_local));
    ASSERT_TRUE(has_watch_path(&w, dot_agents_local));
    ASSERT_TRUE(has_watch_path(&w, dot_claude_base));

    file_watcher_free(&w);
    unlink(dot_claude_base);
    rmdir(dot_claude_dir);
    unlink(dot_agents_local);
    rmdir(dot_agents_dir);
    unlink(compat_local);
    unlink(root_override);
    rmdir(tmpdir);
    return 0;
}

int test_file_watcher_all(void) {
    TEST_SUITE("file_watcher");
    TEST_COUNTERS;

    RUN_TEST(test_init_free);
    RUN_TEST(test_add_nonexistent);
    RUN_TEST(test_add_real_file);
    RUN_TEST(test_add_directory);
    RUN_TEST(test_add_duplicate);
    RUN_TEST(test_wrong_type);
    RUN_TEST(test_add_null);
    RUN_TEST(test_callback_registration);
    RUN_TEST(test_poll_check);
    RUN_TEST(test_path_out_of_range);
    RUN_TEST(test_active_state);
    RUN_TEST(test_start_empty);
    RUN_TEST(test_global);
    RUN_TEST(test_add_config_paths_project_doc_variants);

    TEST_RETURN_FAILED();
}
