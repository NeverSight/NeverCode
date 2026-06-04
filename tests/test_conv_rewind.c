#include "test.h"
#include "nevercode.h"
#include "defaults.h"

static int test_init_empty(void) {
    conv_rewind_init();
    ASSERT_INT_EQ(conv_rewind_count(), 0);
    ASSERT_INT_EQ(conv_rewind_cursor(), -1);
    conv_rewind_free();
    return 0;
}

static int test_checkpoint(void) {
    conv_rewind_init();
    ASSERT_TRUE(conv_rewind_checkpoint(0, 2, "turn 0", "{\"m\":[]}"));
    ASSERT_INT_EQ(conv_rewind_count(), 1);
    ASSERT_INT_EQ(conv_rewind_cursor(), 0);
    conv_rewind_free();
    return 0;
}

static int test_step_back_forward(void) {
    conv_rewind_init();
    conv_rewind_checkpoint(0, 2, "t0", NULL);
    conv_rewind_checkpoint(1, 4, "t1", NULL);
    conv_rewind_checkpoint(2, 6, "t2", NULL);
    ASSERT_INT_EQ(conv_rewind_cursor(), 2);

    ASSERT_TRUE(conv_rewind_step_back());
    ASSERT_INT_EQ(conv_rewind_cursor(), 1);

    ASSERT_TRUE(conv_rewind_step_back());
    ASSERT_INT_EQ(conv_rewind_cursor(), 0);

    ASSERT_FALSE(conv_rewind_step_back());

    ASSERT_TRUE(conv_rewind_step_forward());
    ASSERT_INT_EQ(conv_rewind_cursor(), 1);

    conv_rewind_free();
    return 0;
}

static int test_goto(void) {
    conv_rewind_init();
    conv_rewind_checkpoint(0, 2, "a", NULL);
    conv_rewind_checkpoint(1, 4, "b", NULL);
    ASSERT_TRUE(conv_rewind_goto(0));
    ASSERT_INT_EQ(conv_rewind_cursor(), 0);
    ASSERT_FALSE(conv_rewind_goto(-1));
    ASSERT_FALSE(conv_rewind_goto(5));
    conv_rewind_free();
    return 0;
}

static int test_snapshot(void) {
    conv_rewind_init();
    conv_rewind_checkpoint(0, 2, "s", "{\"data\":1}");
    const char *s = conv_rewind_snapshot(0);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "{\"data\":1}");
    ASSERT_NULL(conv_rewind_snapshot(5));
    conv_rewind_free();
    return 0;
}

static int test_turn_at(void) {
    conv_rewind_init();
    conv_rewind_checkpoint(7, 10, "x", NULL);
    ASSERT_INT_EQ(conv_rewind_turn_at(0), 7);
    ASSERT_INT_EQ(conv_rewind_msg_count_at(0), 10);
    ASSERT_STR_EQ(conv_rewind_label_at(0), "x");
    conv_rewind_free();
    return 0;
}

static int test_truncate_after(void) {
    conv_rewind_init();
    conv_rewind_checkpoint(0, 2, "a", nc_xstrdup("{}"));
    conv_rewind_checkpoint(1, 4, "b", nc_xstrdup("{}"));
    conv_rewind_checkpoint(2, 6, "c", nc_xstrdup("{}"));
    ASSERT_TRUE(conv_rewind_truncate_after(1));
    ASSERT_INT_EQ(conv_rewind_count(), 2);
    ASSERT_INT_EQ(conv_rewind_cursor(), 1);
    conv_rewind_free();
    return 0;
}

static int test_apply(void) {
    conv_rewind_init();
    conv_rewind_checkpoint(0, 2, "a", "{\"v\":1}");
    conv_rewind_checkpoint(1, 4, "b", "{\"v\":2}");
    char *snap = conv_rewind_apply(0);
    ASSERT_NOT_NULL(snap);
    ASSERT_STR_EQ(snap, "{\"v\":1}");
    ASSERT_INT_EQ(conv_rewind_count(), 1);
    free(snap);
    conv_rewind_free();
    return 0;
}

static int test_list(void) {
    conv_rewind_init();
    conv_rewind_checkpoint(0, 2, "first", NULL);
    conv_rewind_checkpoint(1, 4, "second", NULL);
    char *lst = conv_rewind_list();
    ASSERT_NOT_NULL(lst);
    ASSERT_STR_CONTAINS(lst, "first");
    ASSERT_STR_CONTAINS(lst, "second");
    ASSERT_STR_CONTAINS(lst, "<-");
    free(lst);
    conv_rewind_free();
    return 0;
}

static int test_overflow_evicts(void) {
    conv_rewind_init();
    for (int i = 0; i < REWIND_MAX_POINTS + 10; i++) {
        char lbl[32];
        snprintf(lbl, sizeof(lbl), "p%d", i);
        conv_rewind_checkpoint(i, i * 2, lbl, NULL);
    }
    ASSERT_INT_EQ(conv_rewind_count(), REWIND_MAX_POINTS);
    conv_rewind_free();
    return 0;
}

int test_conv_rewind_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("conv_rewind");
    RUN_TEST(test_init_empty);
    RUN_TEST(test_checkpoint);
    RUN_TEST(test_step_back_forward);
    RUN_TEST(test_goto);
    RUN_TEST(test_snapshot);
    RUN_TEST(test_turn_at);
    RUN_TEST(test_truncate_after);
    RUN_TEST(test_apply);
    RUN_TEST(test_list);
    RUN_TEST(test_overflow_evicts);
    TEST_RETURN_FAILED();
}
