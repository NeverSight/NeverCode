#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int test_memory_save_load_roundtrip(void) {
    char tmpdir[] = "/tmp/nevercode_memtest_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));

    char path[512];
    snprintf(path, sizeof(path), "%s/memory.json", tmpdir);

    memory_ctx_t m1;
    memory_init(&m1);
    memory_set(&m1, "project_lang", "C");
    memory_set(&m1, "build_tool", "make");
    memory_set(&m1, "test_cmd", "make test");

    int rc = memory_save(&m1, path);
    ASSERT_INT_EQ(rc, 0);

    memory_ctx_t m2;
    memory_init(&m2);
    int loaded = memory_load(&m2, path);
    ASSERT(loaded == 3);
    ASSERT_STR_EQ(memory_get(&m2, "project_lang"), "C");
    ASSERT_STR_EQ(memory_get(&m2, "build_tool"), "make");
    ASSERT_STR_EQ(memory_get(&m2, "test_cmd"), "make test");

    memory_free(&m1);
    memory_free(&m2);
    unlink(path);
    rmdir(tmpdir);
    return 0;
}

static int test_memory_save_creates_dir(void) {
    char tmpdir[] = "/tmp/nevercode_memtest_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));

    char path[512];
    snprintf(path, sizeof(path), "%s/subdir/memory.json", tmpdir);

    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "key", "value");
    int rc = memory_save(&m, path);
    ASSERT_INT_EQ(rc, 0);

    struct stat st;
    ASSERT(stat(path, &st) == 0);

    memory_free(&m);
    unlink(path);
    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/subdir", tmpdir);
    rmdir(subdir);
    rmdir(tmpdir);
    return 0;
}

static int test_memory_load_nonexistent(void) {
    memory_ctx_t m;
    memory_init(&m);
    int rc = memory_load(&m, "/tmp/nonexistent_memory_12345.json");
    ASSERT(rc == -1);
    ASSERT_INT_EQ(m.len, 0);
    memory_free(&m);
    return 0;
}

static int test_memory_overwrite_existing(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "key1", "val1");
    memory_set(&m, "key1", "val2");
    ASSERT_STR_EQ(memory_get(&m, "key1"), "val2");
    ASSERT_INT_EQ(m.len, 1);
    memory_free(&m);
    return 0;
}

static int test_memory_format_empty(void) {
    memory_ctx_t m;
    memory_init(&m);
    char *prompt = memory_format_for_prompt(&m);
    ASSERT_NULL(prompt);
    memory_free(&m);
    return 0;
}

static int test_memory_save_empty(void) {
    char tmpdir[] = "/tmp/nevercode_memtest_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    char path[512];
    snprintf(path, sizeof(path), "%s/empty.json", tmpdir);

    memory_ctx_t m;
    memory_init(&m);
    int rc = memory_save(&m, path);
    ASSERT_INT_EQ(rc, 0);

    memory_ctx_t m2;
    memory_init(&m2);
    int loaded = memory_load(&m2, path);
    ASSERT(loaded >= 0);
    ASSERT_INT_EQ(m2.len, 0);

    memory_free(&m);
    memory_free(&m2);
    unlink(path);
    rmdir(tmpdir);
    return 0;
}

int test_memory_persist_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("memory_persist");
    RUN_TEST(test_memory_save_load_roundtrip);
    RUN_TEST(test_memory_save_creates_dir);
    RUN_TEST(test_memory_load_nonexistent);
    RUN_TEST(test_memory_overwrite_existing);
    RUN_TEST(test_memory_format_empty);
    RUN_TEST(test_memory_save_empty);
    TEST_RETURN_FAILED();
}
