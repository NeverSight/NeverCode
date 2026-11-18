#include "test.h"
#include "harness.h"
#include "defaults.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
static int t_at_init(void) {
    auto_test_init("/tmp");
    ASSERT_TRUE(auto_test_is_enabled());
    ASSERT_INT_EQ(auto_test_run_count(), 0);
    auto_test_free();
    return 0;
}
static int t_at_en(void) {
    auto_test_init("/tmp");
    auto_test_set_enabled(false);
    ASSERT_FALSE(auto_test_is_enabled());
    auto_test_set_enabled(true);
    ASSERT_TRUE(auto_test_is_enabled());
    auto_test_free();
    return 0;
}
static int t_at_cd(void) {
    auto_test_init("/tmp");
    ASSERT_TRUE(auto_test_should_run(0));
    auto_test_record_run(0, true);
    ASSERT_FALSE(auto_test_should_run(1));
    ASSERT_TRUE(auto_test_should_run(ATEST_COOLDOWN_TURNS));
    auto_test_free();
    return 0;
}
static int t_at_rec(void) {
    auto_test_init("/tmp");
    auto_test_record_run(1, true);
    auto_test_record_run(5, false);
    ASSERT_INT_EQ(auto_test_run_count(), 2);
    ASSERT_INT_EQ(auto_test_pass_count(), 1);
    ASSERT_INT_EQ(auto_test_fail_count(), 1);
    auto_test_free();
    return 0;
}
static int t_at_find(void) {
    mkdir("/tmp/nc_at3", 0755);
    mkdir("/tmp/nc_at3/tests", 0755);
    FILE *f = fopen("/tmp/nc_at3/foo.c", "w");
    if (f) {
        fprintf(f, "x\n");
        fclose(f);
    }
    f = fopen("/tmp/nc_at3/tests/test_foo.c", "w");
    if (f) {
        fprintf(f, "x\n");
        fclose(f);
    }
    auto_test_init("/tmp/nc_at3");
    char *tf = auto_test_find_test_file("/tmp/nc_at3/foo.c");
    ASSERT_NOT_NULL(tf);
    ASSERT_STR_CONTAINS(tf, "test_foo");
    free(tf);
    auto_test_free();
    remove("/tmp/nc_at3/tests/test_foo.c");
    rmdir("/tmp/nc_at3/tests");
    remove("/tmp/nc_at3/foo.c");
    rmdir("/tmp/nc_at3");
    return 0;
}
static int t_at_null(void) {
    auto_test_init(NULL);
    char *tf = auto_test_find_test_file(NULL);
    ASSERT_NULL(tf);
    auto_test_free();
    return 0;
}
static int t_at_fmt(void) {
    auto_test_init("/tmp");
    auto_test_record_run(1, true);
    char *s = auto_test_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Auto-test:");
    free(s);
    auto_test_free();
    return 0;
}
static int t_at_dis(void) {
    auto_test_init("/tmp");
    auto_test_set_enabled(false);
    ASSERT_FALSE(auto_test_should_run(100));
    auto_test_free();
    return 0;
}
static int t_at_build_test_unit(void) {
    const char *root = "/tmp/nc_at_bld";
    mkdir(root, 0755);
    char sub[256];
    snprintf(sub, sizeof(sub), "%s/tests", root);
    mkdir(sub, 0755);
    char p[256];
    snprintf(p, sizeof(p), "%s/Makefile", root);
    FILE *f = fopen(p, "w");
    if (f) {
        fprintf(f, "all:\n");
        fclose(f);
    }
    snprintf(p, sizeof(p), "%s/tests/test_main.c", root);
    f = fopen(p, "w");
    if (f) {
        fprintf(f, "/* stub */\n");
        fclose(f);
    }
    auto_test_init(root);
    char *c = auto_test_build_command("dummy.c");
    ASSERT_NOT_NULL(c);
    ASSERT_STR_EQ(c, "make test-unit");
    free(c);
    auto_test_free();
    snprintf(p, sizeof(p), "%s/tests/test_main.c", root);
    remove(p);
    rmdir(sub);
    snprintf(p, sizeof(p), "%s/Makefile", root);
    remove(p);
    rmdir(root);
    return 0;
}

static int t_at_snapshot_restore(void) {
    char root[256];
    char tests_dir[256];
    char path[256];
    snprintf(root, sizeof(root), "/tmp/nc_at_snap_%d", (int)getpid());
    mkdir(root, 0755);
    snprintf(tests_dir, sizeof(tests_dir), "%.247s/tests", root);
    mkdir(tests_dir, 0755);

    snprintf(path, sizeof(path), "%.244s/Makefile", root);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "all:\n");
        fclose(f);
    }
    snprintf(path, sizeof(path), "%.235s/tests/test_main.c", root);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "/* stub */\n");
        fclose(f);
    }
    snprintf(path, sizeof(path), "%.234s/tests/test_alpha.c", root);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "/* alpha */\n");
        fclose(f);
    }

    auto_test_init(root);
    char *cmd = auto_test_build_command("alpha.c");
    ASSERT_NOT_NULL(cmd);
    ASSERT_STR_EQ(cmd, "make test-unit");
    free(cmd);
    auto_test_record_run(2, true);
    auto_test_set_enabled(false);

    auto_test_snapshot_t snap;
    auto_test_snapshot(&snap);

    auto_test_init("/tmp");
    auto_test_restore(&snap);
    ASSERT_FALSE(auto_test_is_enabled());
    ASSERT_INT_EQ(auto_test_run_count(), 1);
    ASSERT_INT_EQ(auto_test_pass_count(), 1);
    ASSERT_INT_EQ(auto_test_fail_count(), 0);

    auto_test_set_enabled(true);
    ASSERT_FALSE(auto_test_should_run(4));
    ASSERT_TRUE(auto_test_should_run(5));

    cmd = auto_test_build_command("alpha.c");
    ASSERT_NOT_NULL(cmd);
    ASSERT_STR_EQ(cmd, "make test-unit");
    free(cmd);

    char *tf = auto_test_find_test_file("/tmp/elsewhere/alpha.c");
    ASSERT_NOT_NULL(tf);
    ASSERT_STR_CONTAINS(tf, "test_alpha.c");
    free(tf);
    auto_test_free();

    snprintf(path, sizeof(path), "%.234s/tests/test_alpha.c", root);
    remove(path);
    snprintf(path, sizeof(path), "%.235s/tests/test_main.c", root);
    remove(path);
    rmdir(tests_dir);
    snprintf(path, sizeof(path), "%.244s/Makefile", root);
    remove(path);
    rmdir(root);
    return 0;
}

static int t_at_restore_null(void) {
    auto_test_init("/tmp");
    auto_test_record_run(1, true);
    auto_test_set_enabled(false);
    auto_test_restore(NULL);
    ASSERT_TRUE(auto_test_is_enabled());
    ASSERT_INT_EQ(auto_test_run_count(), 0);
    ASSERT_TRUE(auto_test_should_run(0));
    auto_test_free();
    return 0;
}

int test_auto_test_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("auto_test");
    RUN_TEST(t_at_init);
    RUN_TEST(t_at_en);
    RUN_TEST(t_at_cd);
    RUN_TEST(t_at_rec);
    RUN_TEST(t_at_find);
    RUN_TEST(t_at_null);
    RUN_TEST(t_at_fmt);
    RUN_TEST(t_at_dis);
    RUN_TEST(t_at_build_test_unit);
    RUN_TEST(t_at_snapshot_restore);
    RUN_TEST(t_at_restore_null);
    TEST_RETURN_FAILED();
}
