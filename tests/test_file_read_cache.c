/*
 * test_file_read_cache.c — Tests for mtime-based file content cache.
 */
#include "test.h"
#include "nevercode.h"
#include <unistd.h>

static int t_empty(void) {
    frc_clear();
    ASSERT_INT_EQ(frc_size(), 0);
    ASSERT_NULL(frc_get("/nonexistent", NULL));
    return 0;
}

static int t_put_get(void) {
    frc_clear();
    /* Use the Makefile as a test file (guaranteed to exist) */
    const char *path = "Makefile";
    int rc = frc_put(path, "cached content", 14);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(frc_size(), 1);

    size_t len = 0;
    const char *c = frc_get(path, &len);
    ASSERT_NOT_NULL(c);
    ASSERT_INT_EQ((int)len, 14);
    ASSERT_STR_EQ(c, "cached content");

    frc_clear();
    return 0;
}

static int t_invalidate(void) {
    frc_clear();
    frc_put("Makefile", "data", 4);
    ASSERT_INT_EQ(frc_size(), 1);
    frc_invalidate("Makefile");
    ASSERT_INT_EQ(frc_size(), 0);
    ASSERT_NULL(frc_get("Makefile", NULL));
    return 0;
}

static int t_null_safety(void) {
    frc_clear();
    ASSERT_NULL(frc_get(NULL, NULL));
    ASSERT(frc_put(NULL, "x", 1) != 0);
    ASSERT(frc_put("x", NULL, 0) != 0);
    frc_invalidate(NULL);
    return 0;
}

static int t_macros(void) {
    ASSERT(FRC_MAX_ENTRIES > 0);
    return 0;
}

int test_file_read_cache_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("file_read_cache");
    RUN_TEST(t_empty);
    RUN_TEST(t_put_get);
    RUN_TEST(t_invalidate);
    RUN_TEST(t_null_safety);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
