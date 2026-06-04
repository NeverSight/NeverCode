#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <unistd.h>

static int test_memory_init(void) {
    memory_ctx_t m;
    memory_init(&m);
    ASSERT_INT_EQ(m.len, 0);
    ASSERT_NULL(m.entries);
    memory_free(&m);
    return 0;
}

static int test_memory_set_get(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "name", "Alice");
    memory_set(&m, "lang", "C");
    ASSERT_INT_EQ(m.len, 2);
    ASSERT_STR_EQ(memory_get(&m, "name"), "Alice");
    ASSERT_STR_EQ(memory_get(&m, "lang"), "C");
    ASSERT_NULL(memory_get(&m, "missing"));
    memory_free(&m);
    return 0;
}

static int test_memory_overwrite(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "key", "old");
    memory_set(&m, "key", "new");
    ASSERT_INT_EQ(m.len, 1);
    ASSERT_STR_EQ(memory_get(&m, "key"), "new");
    memory_free(&m);
    return 0;
}

static int test_memory_save_load(void) {
    const char *path = "/tmp/nevercode_test_memory.json";
    unlink(path);

    memory_ctx_t m1;
    memory_init(&m1);
    memory_set(&m1, "project", "nevercode");
    memory_set(&m1, "version", "0.2.0");
    memory_set(&m1, "author", "testuser");
    ASSERT_INT_EQ(memory_save(&m1, path), 0);
    memory_free(&m1);

    memory_ctx_t m2;
    memory_init(&m2);
    int loaded = memory_load(&m2, path);
    ASSERT_INT_EQ(loaded, 3);
    ASSERT_STR_EQ(memory_get(&m2, "project"), "nevercode");
    ASSERT_STR_EQ(memory_get(&m2, "version"), "0.2.0");
    ASSERT_STR_EQ(memory_get(&m2, "author"), "testuser");
    memory_free(&m2);
    unlink(path);
    return 0;
}

static int test_memory_load_nonexistent(void) {
    memory_ctx_t m;
    memory_init(&m);
    ASSERT(memory_load(&m, "/tmp/does_not_exist_12345.json") < 0);
    ASSERT_INT_EQ(m.len, 0);
    memory_free(&m);
    return 0;
}

static int test_memory_format_prompt(void) {
    memory_ctx_t m;
    memory_init(&m);
    ASSERT_NULL(memory_format_for_prompt(&m));

    memory_set(&m, "editor", "vim");
    char *prompt = memory_format_for_prompt(&m);
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "User Memories") != NULL);
    ASSERT(strstr(prompt, "editor") != NULL);
    ASSERT(strstr(prompt, "vim") != NULL);
    free(prompt);
    memory_free(&m);
    return 0;
}

static int test_memory_null_args(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, NULL, "val");
    memory_set(&m, "key", NULL);
    ASSERT_INT_EQ(m.len, 0);
    ASSERT_NULL(memory_get(&m, NULL));
    memory_free(&m);
    return 0;
}

static int test_memory_many_entries(void) {
    memory_ctx_t m;
    memory_init(&m);
    for (int i = 0; i < 50; i++) {
        char key[32], val[32];
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(val, sizeof(val), "val_%d", i);
        memory_set(&m, key, val);
    }
    ASSERT_INT_EQ(m.len, 50);
    ASSERT_STR_EQ(memory_get(&m, "key_0"), "val_0");
    ASSERT_STR_EQ(memory_get(&m, "key_49"), "val_49");
    memory_free(&m);
    return 0;
}

int test_memory_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("memory");
    RUN_TEST(test_memory_init);
    RUN_TEST(test_memory_set_get);
    RUN_TEST(test_memory_overwrite);
    RUN_TEST(test_memory_save_load);
    RUN_TEST(test_memory_load_nonexistent);
    RUN_TEST(test_memory_format_prompt);
    RUN_TEST(test_memory_null_args);
    RUN_TEST(test_memory_many_entries);
    TEST_RETURN_FAILED();
}
