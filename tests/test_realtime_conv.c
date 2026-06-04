#include "test.h"
#include "nevercode.h"
#include "config.h"

static int test_init_free(void) {
    realtime_conv_init();
    ASSERT_INT_EQ(realtime_conv_chunk_count(), 0);
    realtime_conv_free();
    return 0;
}

static int test_begin(void) {
    realtime_conv_init();
    ASSERT_INT_EQ(realtime_conv_begin("sess1", "gpt-5.4"), 0);
    realtime_conv_free();
    return 0;
}

static int test_feed_and_assemble(void) {
    realtime_conv_init();
    realtime_conv_begin("s", "m");
    realtime_conv_feed_chunk("Hello ", 2, false);
    realtime_conv_feed_chunk("world", 1, true);
    ASSERT_INT_EQ(realtime_conv_chunk_count(), 2);
    ASSERT_INT_EQ(realtime_conv_tokens_out(), 3);
    char *t = realtime_conv_assembled();
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t, "Hello world");
    free(t);
    realtime_conv_free();
    return 0;
}

static int test_complete_records_input(void) {
    realtime_conv_init();
    realtime_conv_begin("s", "m");
    realtime_conv_complete(50);
    ASSERT_INT_EQ(realtime_conv_tokens_in(), 50);
    realtime_conv_free();
    return 0;
}

static int test_error_adds_chunk(void) {
    realtime_conv_init();
    realtime_conv_begin("s", "m");
    realtime_conv_error("connection lost");
    ASSERT_INT_EQ(realtime_conv_chunk_count(), 1);
    realtime_conv_free();
    return 0;
}

static int test_format(void) {
    realtime_conv_init();
    realtime_conv_begin("s", "m");
    char *f = realtime_conv_format();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Realtime:");
    free(f);
    realtime_conv_free();
    return 0;
}

static int test_default_model_follows_global_config(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.provider = PROVIDER_NATIVE;
    g_cfg_ptr = &cfg;

    realtime_conv_init();
    ASSERT_INT_EQ(realtime_conv_begin("sess1", NULL), 0);
    char *f = realtime_conv_format();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, nc_provider_default_model(PROVIDER_NATIVE));
    free(f);
    realtime_conv_free();

    g_cfg_ptr = NULL;
    config_free(&cfg);
    return 0;
}

int test_realtime_conv_all(void) {
    TEST_SUITE("realtime_conv");
    TEST_COUNTERS;
    RUN_TEST(test_init_free);
    RUN_TEST(test_begin);
    RUN_TEST(test_feed_and_assemble);
    RUN_TEST(test_complete_records_input);
    RUN_TEST(test_error_adds_chunk);
    RUN_TEST(test_format);
    RUN_TEST(test_default_model_follows_global_config);
    TEST_RETURN_FAILED();
}
