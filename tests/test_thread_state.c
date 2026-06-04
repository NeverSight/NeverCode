#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_ts_roundtrip(void) {
    char dir[] = "/tmp/nc_ts_XXXXXX";
    ASSERT(mkdtemp(dir) != NULL);
    thread_state_free();
    thread_state_init();
    ASSERT_INT_EQ(thread_state_capture("t1", "s1", 3, 5, 100, "think"), 0);
    ASSERT_INT_EQ(thread_state_annotate(0, "n1"), 0);
    ASSERT_INT_EQ(thread_state_save(dir), 0);
    thread_state_free();
    thread_state_init();
    ASSERT_INT_EQ(thread_state_load(dir), 0);
    ASSERT_INT_EQ(thread_state_count(), 1);
    ASSERT_INT_EQ(thread_state_find("t1"), 0);
    char *f = thread_state_format(0);
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "t1");
    ASSERT_STR_CONTAINS(f, "n1");
    free(f);
    char *all = thread_state_format_all();
    ASSERT_NOT_NULL(all);
    free(all);
    thread_state_free();
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, "nc_thread_state.json");
    unlink(path);
    rmdir(dir);
    return 0;
}

int test_thread_state_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("thread_state");
    RUN_TEST(test_ts_roundtrip);
    TEST_RETURN_FAILED();
}
