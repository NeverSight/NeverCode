/*
 * test_activity_heatmap.c — Tests for terminal activity heatmap.
 */
#include "test.h"
#include "nevercode.h"

static int t_null(void) {
    ASSERT_NULL(activity_heatmap_render(NULL, 0, 80));
    return 0;
}

static int t_empty_data(void) {
    int counts[7] = {0};
    char *r = activity_heatmap_render(counts, 7, 80);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "no activity");
    free(r);
    return 0;
}

static int t_basic_render(void) {
    int counts[14] = {0, 5, 3, 0, 10, 2, 1, 4, 8, 0, 6, 1, 0, 7};
    char *r = activity_heatmap_render(counts, 14, 80);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "Mon");
    ASSERT_STR_CONTAINS(r, "Wed");
    ASSERT_STR_CONTAINS(r, "Fri");
    free(r);
    return 0;
}

static int t_legend(void) {
    int counts[7] = {1, 2, 3, 4, 5, 6, 7};
    char *r = activity_heatmap_render(counts, 7, 80);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "low");
    ASSERT_STR_CONTAINS(r, "med");
    ASSERT_STR_CONTAINS(r, "high");
    free(r);
    return 0;
}

static int t_macros(void) {
    ASSERT(HEATMAP_MAX_WEEKS > 0);
    ASSERT(HEATMAP_MIN_WEEKS > 0);
    ASSERT(HEATMAP_MAX_DAYS > 0);
    return 0;
}

int test_activity_heatmap_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("activity_heatmap");
    RUN_TEST(t_null);
    RUN_TEST(t_empty_data);
    RUN_TEST(t_basic_render);
    RUN_TEST(t_legend);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
