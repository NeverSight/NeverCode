#include "test.h"
#include "nevercode.h"
#include "defaults.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static int test_scratch_write_read(void) {
    scratchpad_init();
    ASSERT_INT_EQ(scratchpad_write("key1", "value1", "agent-1"), 0);
    ASSERT_STR_EQ(scratchpad_read("key1"), "value1");
    ASSERT_INT_EQ(scratchpad_count(), 1);
    scratchpad_free();
    return 0;
}

static int test_scratch_overwrite(void) {
    scratchpad_init();
    scratchpad_write("k", "v1", NULL);
    scratchpad_write("k", "v2", "agent-2");
    ASSERT_STR_EQ(scratchpad_read("k"), "v2");
    ASSERT_INT_EQ(scratchpad_count(), 1);
    scratchpad_free();
    return 0;
}

static int test_scratch_remove(void) {
    scratchpad_init();
    scratchpad_write("a", "1", NULL);
    scratchpad_write("b", "2", NULL);
    ASSERT_INT_EQ(scratchpad_remove("a"), 0);
    ASSERT_NULL(scratchpad_read("a"));
    ASSERT_STR_EQ(scratchpad_read("b"), "2");
    ASSERT_INT_EQ(scratchpad_count(), 1);
    scratchpad_free();
    return 0;
}

static int test_scratch_remove_missing(void) {
    scratchpad_init();
    ASSERT_INT_EQ(scratchpad_remove("nope"), -1);
    scratchpad_free();
    return 0;
}

static int test_scratch_keys(void) {
    scratchpad_init();
    scratchpad_write("x", "1", NULL);
    scratchpad_write("y", "2", NULL);
    const char *keys[4];
    int n = scratchpad_keys(keys, 4);
    ASSERT_INT_EQ(n, 2);
    ASSERT_STR_EQ(keys[0], "x");
    ASSERT_STR_EQ(keys[1], "y");
    scratchpad_free();
    return 0;
}

static int test_scratch_format(void) {
    scratchpad_init();
    scratchpad_write("findings", "bug in parser", "worker-1");
    char *s = scratchpad_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "findings");
    ASSERT_STR_CONTAINS(s, "bug in parser");
    ASSERT_STR_CONTAINS(s, "worker-1");
    free(s);
    scratchpad_free();
    return 0;
}

static int test_scratch_format_empty(void) {
    scratchpad_init();
    char *s = scratchpad_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "empty");
    free(s);
    scratchpad_free();
    return 0;
}

static int test_scratch_persist(void) {
    scratchpad_init();
    scratchpad_write("a", "alpha", "w1");
    scratchpad_write("b", "beta", NULL);

    char path[128];
    snprintf(path, sizeof(path), "/tmp/nevercode_scratch_%d.txt", (int)getpid());
    ASSERT_INT_EQ(scratchpad_persist(path), 0);

    FILE *f = fopen(path, "r");
    ASSERT_NOT_NULL(f);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    unlink(path);

    ASSERT(strstr(buf, "alpha") != NULL);
    ASSERT(strstr(buf, "beta") != NULL);
    scratchpad_free();
    return 0;
}

static int test_scratch_auto_init(void) {
    scratchpad_free();
    ASSERT_INT_EQ(scratchpad_write("auto", "yes", NULL), 0);
    ASSERT_STR_EQ(scratchpad_read("auto"), "yes");
    scratchpad_free();
    return 0;
}

int test_scratchpad_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("scratchpad");

    RUN_TEST(test_scratch_write_read);
    RUN_TEST(test_scratch_overwrite);
    RUN_TEST(test_scratch_remove);
    RUN_TEST(test_scratch_remove_missing);
    RUN_TEST(test_scratch_keys);
    RUN_TEST(test_scratch_format);
    RUN_TEST(test_scratch_format_empty);
    RUN_TEST(test_scratch_persist);
    RUN_TEST(test_scratch_auto_init);

    TEST_RETURN_FAILED();
}
