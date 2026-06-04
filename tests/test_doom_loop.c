#include "test.h"
#include "nevercode.h"

static int t_init_clean(void) {
    doom_loop_init();
    ASSERT_INT_EQ(doom_loop_fire_count(), 0);
    ASSERT_NULL(doom_loop_last_tool());
    doom_loop_reset();
    return 0;
}

static int t_no_fire_varied(void) {
    doom_loop_init();
    ASSERT_FALSE(doom_loop_record("read", "{\"path\":\"a.c\"}"));
    ASSERT_FALSE(doom_loop_record("write", "{\"path\":\"b.c\"}"));
    ASSERT_FALSE(doom_loop_record("grep", "{\"pattern\":\"foo\"}"));
    ASSERT_INT_EQ(doom_loop_fire_count(), 0);
    return 0;
}

static int t_fire_repeated(void) {
    doom_loop_init();
    const char *tool = "read";
    const char *input = "{\"path\":\"same.c\"}";
    for (int i = 0; i < DOOM_LOOP_THRESHOLD - 1; i++)
        ASSERT_FALSE(doom_loop_record(tool, input));
    ASSERT_TRUE(doom_loop_record(tool, input));
    ASSERT_TRUE(doom_loop_fire_count() >= 1);
    ASSERT_STR_EQ(doom_loop_last_tool(), "read");
    return 0;
}

static int t_different_input_no_fire(void) {
    doom_loop_init();
    ASSERT_FALSE(doom_loop_record("bash", "{\"cmd\":\"ls\"}"));
    ASSERT_FALSE(doom_loop_record("bash", "{\"cmd\":\"pwd\"}"));
    ASSERT_FALSE(doom_loop_record("bash", "{\"cmd\":\"cat foo\"}"));
    ASSERT_INT_EQ(doom_loop_fire_count(), 0);
    return 0;
}

static int t_reset_clears(void) {
    doom_loop_init();
    for (int i = 0; i < DOOM_LOOP_THRESHOLD; i++)
        doom_loop_record("tool", "{\"x\":1}");
    ASSERT_TRUE(doom_loop_fire_count() > 0);
    doom_loop_reset();
    ASSERT_INT_EQ(doom_loop_fire_count(), 0);
    return 0;
}

static int t_disabled_never_fires(void) {
    doom_loop_init();
    doom_loop_set_enabled(false);
    for (int i = 0; i < DOOM_LOOP_THRESHOLD + 5; i++)
        ASSERT_FALSE(doom_loop_record("tool", "{\"x\":1}"));
    ASSERT_INT_EQ(doom_loop_fire_count(), 0);
    return 0;
}

static int t_format_status(void) {
    doom_loop_init();
    char *s = doom_loop_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "active");
    free(s);
    return 0;
}

static int t_snapshot_restore(void) {
    doom_loop_init();
    const char *tool = "read";
    const char *input = "{\"path\":\"same.c\"}";
    for (int i = 0; i < DOOM_LOOP_THRESHOLD - 1; i++)
        ASSERT_FALSE(doom_loop_record(tool, input));

    doom_loop_snapshot_t snap;
    doom_loop_snapshot(&snap);
    ASSERT_INT_EQ(snap.count, DOOM_LOOP_THRESHOLD - 1);
    ASSERT_TRUE(snap.enabled);

    doom_loop_reset();
    doom_loop_restore(&snap);
    ASSERT_STR_EQ(doom_loop_last_tool(), "read");
    ASSERT_TRUE(doom_loop_record(tool, input));
    ASSERT_INT_EQ(doom_loop_fire_count(), 1);
    return 0;
}

int test_doom_loop_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("doom_loop");
    RUN_TEST(t_init_clean);
    RUN_TEST(t_no_fire_varied);
    RUN_TEST(t_fire_repeated);
    RUN_TEST(t_different_input_no_fire);
    RUN_TEST(t_reset_clears);
    RUN_TEST(t_disabled_never_fires);
    RUN_TEST(t_format_status);
    RUN_TEST(t_snapshot_restore);
    TEST_RETURN_FAILED();
}
