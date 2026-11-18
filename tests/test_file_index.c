#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_tmpdir[512];

static void make_tmpdir(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/nc_fidx_test_XXXXXX");
    if (mkdtemp(g_tmpdir) == NULL) {
        fprintf(stderr, "mkdtemp failed\n");
        exit(1);
    }
}

static void write_file(const char *rel, const char *content) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", g_tmpdir, rel);

    char *slash = strrchr(path, '/');
    if (slash) {
        char dir[1024];
        size_t dlen = (size_t)(slash - path);
        memcpy(dir, path, dlen);
        dir[dlen] = '\0';
        mkdir(dir, 0755);
    }
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content ? content : "", f);
        fclose(f);
    }
}

static void cleanup_tmpdir(void) {
    nc_rm_rf(g_tmpdir);
}

static int test_init_free(void) {
    file_index_init();
    ASSERT_FALSE(file_index_ready());
    ASSERT_INT_EQ(file_index_count(), 0);
    file_index_free();
    return 0;
}

static int test_lookup_before_build(void) {
    file_index_init();
    ASSERT_FALSE(file_index_lookup("nonexistent.c"));
    file_index_free();
    return 0;
}

static int test_format_stats(void) {
    file_index_init();
    char *stats = file_index_format_stats();
    ASSERT_NOT_NULL(stats);
    ASSERT_TRUE(strlen(stats) > 0);
    free(stats);
    file_index_free();
    return 0;
}

static int test_build_walk(void) {
    make_tmpdir();
    write_file("main.c", "int main(){}");
    write_file("lib.h", "#pragma once");
    write_file("src/utils.c", "void util(){}");
    write_file("src/deep/nested.c", "void nested(){}");

    int n = file_index_build(g_tmpdir);
    ASSERT_GT(n, 0);
    ASSERT_TRUE(file_index_ready());
    ASSERT_INT_EQ(file_index_count(), n);

    ASSERT_TRUE(file_index_lookup("main.c"));
    ASSERT_TRUE(file_index_lookup("lib.h"));
    ASSERT_TRUE(file_index_lookup("src/utils.c"));
    ASSERT_TRUE(file_index_lookup("src/deep/nested.c"));
    ASSERT_FALSE(file_index_lookup("nonexistent.c"));

    file_index_free();
    cleanup_tmpdir();
    return 0;
}

static int test_build_invalid(void) {
    ASSERT_TRUE(file_index_build(NULL) < 0);
    ASSERT_TRUE(file_index_build("/nonexistent_path_xyz") < 0);
    return 0;
}

static int test_glob(void) {
    make_tmpdir();
    write_file("foo.c", "");
    write_file("bar.c", "");
    write_file("baz.h", "");

    file_index_build(g_tmpdir);
    const char *out[10];
    int n = file_index_glob("*.c", out, 10);
    ASSERT_INT_EQ(n, 2);

    int nh = file_index_glob("*.h", out, 10);
    ASSERT_INT_EQ(nh, 1);

    file_index_free();
    cleanup_tmpdir();
    return 0;
}

static int test_prefix(void) {
    make_tmpdir();
    write_file("src/a.c", "");
    write_file("src/b.c", "");
    write_file("lib/c.c", "");

    file_index_build(g_tmpdir);
    const char *out[10];
    int n = file_index_prefix("src/", out, 10);
    ASSERT_INT_EQ(n, 2);
    int nl = file_index_prefix("lib/", out, 10);
    ASSERT_INT_EQ(nl, 1);

    file_index_free();
    cleanup_tmpdir();
    return 0;
}

static int test_age(void) {
    make_tmpdir();
    write_file("x.c", "");
    file_index_build(g_tmpdir);
    long age = file_index_age_s();
    ASSERT_GE((int)age, 0);
    ASSERT_TRUE(age < 5);
    file_index_free();
    cleanup_tmpdir();
    return 0;
}

static int test_root(void) {
    make_tmpdir();
    write_file("y.c", "");
    file_index_build(g_tmpdir);
    const char *r = file_index_root();
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "nc_fidx_test");
    file_index_free();
    cleanup_tmpdir();
    return 0;
}

int test_file_index_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("file_index");
    RUN_TEST(test_init_free);
    RUN_TEST(test_lookup_before_build);
    RUN_TEST(test_format_stats);
    RUN_TEST(test_build_walk);
    RUN_TEST(test_build_invalid);
    RUN_TEST(test_glob);
    RUN_TEST(test_prefix);
    RUN_TEST(test_age);
    RUN_TEST(test_root);
    TEST_RETURN_FAILED();
}
