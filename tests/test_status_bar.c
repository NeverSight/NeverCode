#include "test.h"
#include "nevercode.h"
#include "config.h"
#include "defaults.h"

static int test_init_free(void) {
    sbar_init();
    ASSERT_NOT_NULL(sbar_get_model());
    ASSERT_NOT_NULL(sbar_get_mode());
    sbar_free();
    return 0;
}

static int test_set_model(void) {
    sbar_init();
    sbar_set_model("test-model-v1");
    ASSERT_STR_EQ(sbar_get_model(), "test-model-v1");
    sbar_free();
    return 0;
}

static int test_set_mode(void) {
    sbar_init();
    sbar_set_mode("plan");
    ASSERT_STR_EQ(sbar_get_mode(), "plan");
    sbar_free();
    return 0;
}

static int test_dirty_flag(void) {
    sbar_init();
    ASSERT_TRUE(sbar_is_dirty());
    char *r = sbar_render();
    ASSERT_NOT_NULL(r);
    ASSERT_FALSE(sbar_is_dirty());
    sbar_set_turns(5);
    ASSERT_TRUE(sbar_is_dirty());
    free(r);
    sbar_free();
    return 0;
}

static int test_render_contains_model(void) {
    sbar_init();
    sbar_set_model("xmodel");
    char *r = sbar_render();
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "xmodel");
    free(r);
    sbar_free();
    return 0;
}

static int test_render_contains_mode(void) {
    sbar_init();
    sbar_set_mode("debug");
    char *r = sbar_render();
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "debug");
    free(r);
    sbar_free();
    return 0;
}

static int test_tokens_display(void) {
    sbar_init();
    sbar_set_tokens(1500, 300);
    char *r = sbar_render();
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "1.5k");
    free(r);
    sbar_free();
    return 0;
}

static int test_null_safety(void) {
    sbar_init();
    sbar_set_model(NULL);
    sbar_set_mode(NULL);
    char *r = sbar_render();
    ASSERT_NOT_NULL(r);
    free(r);
    sbar_free();
    return 0;
}

static int test_turns(void) {
    sbar_init();
    sbar_set_turns(42);
    ASSERT_INT_EQ(sbar_get_turns(), 42);
    sbar_free();
    return 0;
}

static int test_init_uses_global_config_model(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.provider = PROVIDER_NATIVE;
    g_cfg_ptr = &cfg;

    sbar_init();
    ASSERT_STR_EQ(sbar_get_model(), nc_provider_default_model(PROVIDER_NATIVE));
    sbar_free();

    g_cfg_ptr = NULL;
    config_free(&cfg);
    return 0;
}

static int test_render_respects_narrow_width(void) {
    sbar_init();
    sbar_set_model("very-long-model-name-for-status-bar-width-test");
    sbar_set_mode("agent");
    sbar_set_tokens(123456, 654321);
    sbar_set_cost(0.1234);
    sbar_set_elapsed(3661);
    sbar_set_turns(17);
    char *r = sbar_render_for_width(24);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(line_visible_width(r) <= 24);
    ASSERT_STR_CONTAINS(r, "T17");
    free(r);
    sbar_free();
    return 0;
}

static int test_render_width_fallback_when_invalid(void) {
    sbar_init();
    sbar_set_turns(3);
    char *r = sbar_render_for_width(0);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(line_visible_width(r) <= SBAR_MIN_TERM_WIDTH);
    ASSERT_STR_CONTAINS(r, "T3");
    free(r);
    sbar_free();
    return 0;
}

static int test_render_cjk_model_width(void) {
    sbar_init();
    /* CJK chars occupy 2 terminal columns each */
    sbar_set_model("\xe4\xb8\xad\xe6\x96\x87\xe6\xa8\xa1\xe5\x9e\x8b"); /* 中文模型 = 8 cols */
    sbar_set_mode("agent");
    sbar_set_turns(5);

    char *r = sbar_render_for_width(40);
    ASSERT_NOT_NULL(r);
    int vis = line_visible_width(r);
    ASSERT_TRUE(vis <= 40);
    ASSERT_STR_CONTAINS(r, "T5");
    free(r);

    /* narrow width forces truncation of CJK model name */
    r = sbar_render_for_width(24);
    ASSERT_NOT_NULL(r);
    vis = line_visible_width(r);
    ASSERT_TRUE(vis <= 24);
    free(r);

    sbar_free();
    return 0;
}

static int test_copy_prefix_cells_cjk(void) {
    char out[64];
    /* 3 CJK chars = 6 cols; max_cells=4 should keep only 2 chars (4 cols) */
    line_copy_prefix_cells("\xe4\xb8\xad\xe6\x96\x87\xe5\xa5\xbd", 4, out, sizeof(out));
    int vis = line_visible_width(out);
    ASSERT_TRUE(vis <= 4);
    ASSERT_TRUE(vis >= 2);

    /* ASCII still works: 5 chars, max 3 cells */
    line_copy_prefix_cells("hello", 3, out, sizeof(out));
    ASSERT_INT_EQ(line_visible_width(out), 3);
    ASSERT_TRUE(nc_starts_with(out, "hel"));

    return 0;
}

int test_status_bar_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("status_bar");
    RUN_TEST(test_init_free);
    RUN_TEST(test_set_model);
    RUN_TEST(test_set_mode);
    RUN_TEST(test_dirty_flag);
    RUN_TEST(test_render_contains_model);
    RUN_TEST(test_render_contains_mode);
    RUN_TEST(test_tokens_display);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_turns);
    RUN_TEST(test_init_uses_global_config_model);
    RUN_TEST(test_render_respects_narrow_width);
    RUN_TEST(test_render_width_fallback_when_invalid);
    RUN_TEST(test_render_cjk_model_width);
    RUN_TEST(test_copy_prefix_cells_cjk);
    TEST_RETURN_FAILED();
}
