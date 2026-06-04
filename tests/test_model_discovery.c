#include "test.h"
#include "nevercode.h"
static int t1(void) {
    model_discovery_clear();
    ASSERT_INT_EQ(model_discovery_count(), 0);
    return 0;
}
static int t2(void) {
    model_discovery_clear();
    ASSERT_FALSE(model_discovery_cache_valid());
    return 0;
}
static int t3(void) {
    model_discovery_clear();
    ASSERT_TRUE(model_discovery_exists("gpt-5.4"));
    return 0;
}
static int t4(void) {
    model_discovery_clear();
    ASSERT_FALSE(model_discovery_exists("fake-xyz"));
    return 0;
}
static int t5(void) {
    model_discovery_clear();
    ASSERT(model_discovery_context_window("gpt-5.4") > 0);
    return 0;
}
static int t6(void) {
    model_discovery_clear();
    char *f = model_discovery_format();
    ASSERT_NOT_NULL(f);
    free(f);
    return 0;
}
static int t7(void) {
    ASSERT(model_discovery_fetch("http://127.0.0.1:1", "fake") < 0);
    return 0;
}
static int t8(void) {
    ASSERT(model_discovery_fetch(NULL, NULL) < 0);
    ASSERT_FALSE(model_discovery_exists(NULL));
    return 0;
}
static int t9(void) {
    ASSERT_NULL(model_discovery_id(-1));
    ASSERT_NULL(model_discovery_name(-1));
    return 0;
}
static int t_unknown_returns_probe_default(void) {
    model_discovery_clear();
    int ctx = model_discovery_context_window("totally-unknown-model-xyz");
    ASSERT_INT_EQ(ctx, MODEL_CTX_PROBE_TIER_0);
    return 0;
}
int test_model_discovery_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("model_discovery");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(t_unknown_returns_probe_default);
    TEST_RETURN_FAILED();
}
