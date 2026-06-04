#include "test.h"
#include "nevercode.h"

static int test_basic_tracking(void) {
    ctx_ref_init();
    ctx_ref_set_turn(1);
    ctx_ref_track("/src/main.c");
    ctx_ref_track("/src/utils.c");
    ASSERT_INT_EQ(ctx_ref_count(), 2);
    ASSERT_TRUE(ctx_ref_is_active("/src/main.c", 5));
    ASSERT_FALSE(ctx_ref_is_active("/src/missing.c", 5));
    ctx_ref_clear();
    return 0;
}

static int test_mention_count(void) {
    ctx_ref_init();
    ctx_ref_set_turn(1);
    ctx_ref_track("/src/main.c");
    ctx_ref_track("/src/main.c");
    ctx_ref_track("/src/main.c");
    ASSERT_INT_EQ(ctx_ref_count(), 1);
    ctx_ref_clear();
    return 0;
}

static int test_recency(void) {
    ctx_ref_init();
    ctx_ref_set_turn(1);
    ctx_ref_track("/old.c");
    ctx_ref_set_turn(10);
    ctx_ref_track("/new.c");
    ASSERT_FALSE(ctx_ref_is_active("/old.c", 3));
    ASSERT_TRUE(ctx_ref_is_active("/new.c", 3));
    ctx_ref_clear();
    return 0;
}

static int test_pinning(void) {
    ctx_ref_init();
    ctx_ref_set_turn(1);
    ctx_ref_track("/pinned.c");
    ctx_ref_pin("/pinned.c");
    ctx_ref_set_turn(100);
    ASSERT_TRUE(ctx_ref_is_active("/pinned.c", 3));
    ctx_ref_clear();
    return 0;
}

static int test_format(void) {
    ctx_ref_init();
    ctx_ref_set_turn(1);
    ctx_ref_track("/a.c");
    ctx_ref_track("/b.c");
    char *fmt = ctx_ref_format_active(5);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "/a.c");
    ASSERT_STR_CONTAINS(fmt, "/b.c");
    free(fmt);
    ctx_ref_clear();
    return 0;
}

static int test_active_count(void) {
    ctx_ref_init();
    ctx_ref_set_turn(1);
    ctx_ref_track("/old1.c");
    ctx_ref_track("/old2.c");
    ctx_ref_set_turn(10);
    ctx_ref_track("/new1.c");
    ASSERT_INT_EQ(ctx_ref_active_count(3), 1);
    ASSERT_INT_EQ(ctx_ref_active_count(20), 3);
    ctx_ref_clear();
    return 0;
}

static int test_null_safety(void) {
    ctx_ref_init();
    ctx_ref_track(NULL);
    ctx_ref_track("");
    ASSERT_INT_EQ(ctx_ref_count(), 0);
    ASSERT_FALSE(ctx_ref_is_active(NULL, 5));
    char *fmt = ctx_ref_format_active(5);
    ASSERT_NULL(fmt);
    ctx_ref_clear();
    return 0;
}

int test_ctx_ref_tracker_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("ctx_ref_tracker");
    RUN_TEST(test_basic_tracking);
    RUN_TEST(test_mention_count);
    RUN_TEST(test_recency);
    RUN_TEST(test_pinning);
    RUN_TEST(test_format);
    RUN_TEST(test_active_count);
    RUN_TEST(test_null_safety);
    TEST_RETURN_FAILED();
}
