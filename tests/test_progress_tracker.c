#include "test.h"
#include "nevercode.h"
#include "harness.h"
#include <string.h>
#include <stdio.h>
static int t_basic(void) {
    progress_tracker_init();
    ASSERT_INT_EQ(progress_tracker_count(), 0);
    int idx = progress_tracker_add("Feature A", false);
    ASSERT_INT_EQ(idx, 0);
    ASSERT_INT_EQ(progress_tracker_count(), 1);
    progress_tracker_add("Bug B", true);
    ASSERT_INT_EQ(progress_tracker_done_count(), 1);
    progress_tracker_mark_done(0);
    ASSERT_INT_EQ(progress_tracker_done_count(), 2);
    ASSERT_INT_EQ(progress_tracker_pending_count(), 0);
    progress_tracker_free();
    return 0;
}
static int t_format(void) {
    progress_tracker_init();
    progress_tracker_add("T1", false);
    progress_tracker_add("T2", true);
    progress_tracker_add("T3", false);
    char *s = progress_tracker_format();
    ASSERT(s != NULL);
    ASSERT(strstr(s, "1/3 completed") != NULL);
    ASSERT(strstr(s, "(2 pending)") != NULL);
    free(s);
    progress_tracker_free();
    return 0;
}
static int t_context(void) {
    progress_tracker_init();
    ASSERT(progress_tracker_context() == NULL);
    progress_tracker_add("Pending", false);
    progress_tracker_add("Done", true);
    char *ctx = progress_tracker_context();
    ASSERT(ctx != NULL);
    ASSERT(strstr(ctx, "<progress_log>") != NULL);
    ASSERT(strstr(ctx, "Pending") != NULL);
    ASSERT(strstr(ctx, "Done") == NULL);
    free(ctx);
    progress_tracker_free();
    return 0;
}
static int t_overflow(void) {
    progress_tracker_init();
    for (int i = 0; i < PROGRESS_MAX_ENTRIES; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Task %d", i);
        ASSERT_INT_EQ(progress_tracker_add(buf, false), i);
    }
    ASSERT_INT_EQ(progress_tracker_add("overflow", false), -1);
    progress_tracker_free();
    return 0;
}
static int t_save_load(void) {
    progress_tracker_init();
    progress_tracker_add("Alpha", false);
    progress_tracker_add("Beta", true);
    ASSERT_INT_EQ(progress_tracker_save(), 0);
    progress_tracker_free();
    progress_tracker_init();
    progress_tracker_load();
    ASSERT_INT_EQ(progress_tracker_count(), 2);
    ASSERT_INT_EQ(progress_tracker_pending_count(), 1);
    ASSERT_INT_EQ(progress_tracker_done_count(), 1);
    char *s = progress_tracker_format();
    ASSERT(strstr(s, "Alpha") != NULL);
    ASSERT(strstr(s, "Beta") != NULL);
    free(s);
    remove(PROGRESS_FILE);
    progress_tracker_free();
    return 0;
}
static int t_load_same_session(void) {
    progress_tracker_init();
    progress_tracker_add("Rehydrate", true);
    ASSERT_INT_EQ(progress_tracker_save(), 0);
    progress_tracker_load();
    ASSERT_INT_EQ(progress_tracker_count(), 1);
    ASSERT_INT_EQ(progress_tracker_done_count(), 1);
    remove(PROGRESS_FILE);
    progress_tracker_free();
    return 0;
}
int test_progress_tracker_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("progress_tracker");
    RUN_TEST(t_basic);
    RUN_TEST(t_format);
    RUN_TEST(t_context);
    RUN_TEST(t_overflow);
    RUN_TEST(t_save_load);
    RUN_TEST(t_load_same_session);
    TEST_RETURN_FAILED();
}
