#include "test.h"
#include "nevercode.h"
#include "api.h"
#include "config.h"
static int t1(void) {
    cache_hint_init();
    ASSERT_TRUE(cache_hint_enabled());
    ASSERT_INT_EQ(cache_hint_hits(), 0);
    ASSERT_INT_EQ(cache_hint_misses(), 0);
    return 0;
}
static int t2(void) {
    cache_hint_init();
    cache_hint_set_enabled(false);
    ASSERT_FALSE(cache_hint_enabled());
    cache_hint_set_enabled(true);
    ASSERT_TRUE(cache_hint_enabled());
    return 0;
}
static int t3(void) {
    cache_hint_init();
    int b = cache_hint_analyze("Hello world system", NULL);
    ASSERT_INT_EQ(b, -1);
    ASSERT_INT_EQ(cache_hint_misses(), 1);
    return 0;
}
static int t4(void) {
    cache_hint_init();
    const char *p = "Stable prompt that does not change between turns at all yes.";
    cache_hint_analyze(p, NULL);
    int b = cache_hint_analyze(p, p);
    ASSERT_INT_EQ(b, -1);
    ASSERT_INT_EQ(cache_hint_hits(), 1);
    return 0;
}
static int t5(void) {
    cache_hint_init();
    char pv[512], cu[512];
    memset(pv, 'A', 400);
    pv[400] = 0;
    memcpy(cu, pv, 400);
    cu[400] = 0;
    nc_strlcpy(cu + 400, " extra suffix", sizeof(cu) - 400);
    cache_hint_analyze(pv, NULL);
    int b = cache_hint_analyze(cu, pv);
    ASSERT_GT(b, 0);
    return 0;
}
static int t6(void) {
    cache_hint_init();
    cache_hint_set_enabled(false);
    ASSERT_INT_EQ(cache_hint_analyze("t", NULL), -1);
    return 0;
}
static int t7(void) {
    cache_hint_init();
    cache_hint_record_hit();
    cache_hint_record_miss();
    ASSERT_INT_EQ(cache_hint_hits(), 1);
    ASSERT_INT_EQ(cache_hint_misses(), 1);
    return 0;
}
static int t8(void) {
    cache_hint_init();
    cache_hint_record_hit();
    char *s = cache_hint_format();
    ASSERT_STR_CONTAINS(s, "Cache");
    free(s);
    return 0;
}
static int t9(void) {
    cache_hint_init();
    ASSERT_INT_EQ(cache_hint_analyze(NULL, NULL), -1);
    return 0;
}
static int t10_tools_hash_stable(void) {
    cache_hint_init();
    cache_hint_tick();
    tool_def_t tools[2] = {
        {.name = "read_file", .description = "Read a file from disk"},
        {.name = "write_file", .description = "Write content to a file"},
    };
    cache_hint_set_tools_hash(tools, 2);
    unsigned long h1 = cache_hint_tools_hash();
    cache_hint_set_tools_hash(tools, 2);
    unsigned long h2 = cache_hint_tools_hash();
    ASSERT_INT_EQ((int)(h1 == h2), 1);
    ASSERT_INT_EQ(cache_hint_tool_breaks(), 0);
    return 0;
}

static int t11_tools_hash_changes(void) {
    cache_hint_init();
    tool_def_t tools1[1] = {{.name = "read_file", .description = "Read a file"}};
    tool_def_t tools2[2] = {
        {.name = "read_file", .description = "Read a file"},
        {.name = "write_file", .description = "Write a file"},
    };
    cache_hint_set_tools_hash(tools1, 1);
    cache_hint_tick();
    cache_hint_set_tools_hash(tools2, 2);
    ASSERT_INT_EQ(cache_hint_tool_breaks(), 1);
    return 0;
}

static int t12_model_hash_changes(void) {
    cache_hint_init();
    cache_hint_set_model_hash("gpt-5.4");
    cache_hint_tick();
    cache_hint_set_model_hash("gpt-4.1-mini");
    ASSERT_INT_EQ(cache_hint_model_breaks(), 1);
    return 0;
}

static int t13_call_count(void) {
    cache_hint_init();
    ASSERT_INT_EQ(cache_hint_call_count(), 0);
    cache_hint_tick();
    cache_hint_tick();
    cache_hint_tick();
    ASSERT_INT_EQ(cache_hint_call_count(), 3);
    return 0;
}

static int t14_format_extended(void) {
    cache_hint_init();
    tool_def_t tools1[1] = {{.name = "bash", .description = "Run commands"}};
    cache_hint_set_tools_hash(tools1, 1);
    cache_hint_tick();
    cache_hint_set_model_hash("gpt-5.4");
    cache_hint_tick();
    cache_hint_set_model_hash("gpt-4.1");
    char *s = cache_hint_format();
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "tool=") != NULL);
    ASSERT(strstr(s, "model=") != NULL);
    ASSERT(strstr(s, "calls=") != NULL);
    free(s);
    return 0;
}

static int t15_api_stream_record_cache(void) {
    cache_hint_init();
    config_t cfg;
    config_init(&cfg);
    cfg.provider = PROVIDER_COMPAT;
    cfg.model = nc_xstrdup("test-model-x");
    cfg.compat_base_url = nc_xstrdup("http://x/v1");
    cfg.compat_api_key = nc_xstrdup("k");
    tool_def_t td[1] = {{.name = "alpha", .description = "beta"}};
    ASSERT_INT_EQ(cache_hint_call_count(), 0);
    api_stream_record_cache(&cfg, td, 1);
    ASSERT_INT_EQ(cache_hint_call_count(), 1);
    api_stream_record_cache(&cfg, td, 1);
    ASSERT_INT_EQ(cache_hint_call_count(), 2);
    ASSERT_INT_EQ(cache_hint_tool_breaks(), 0);
    config_free(&cfg);
    return 0;
}

int test_cache_hint_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("cache_hint");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(t10_tools_hash_stable);
    RUN_TEST(t11_tools_hash_changes);
    RUN_TEST(t12_model_hash_changes);
    RUN_TEST(t13_call_count);
    RUN_TEST(t14_format_extended);
    RUN_TEST(t15_api_stream_record_cache);
    TEST_RETURN_FAILED();
}
