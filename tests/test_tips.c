#include "test.h"
#include "nevercode.h"
#include <string.h>

static int test_tips_init(void) {
    tips_init();
    ASSERT(tips_total() > 0);
    return 0;
}

static int test_tips_total(void) {
    ASSERT(tips_total() >= 20);
    return 0;
}

static int test_tips_get_by_index(void) {
    const char *tip = tips_get_by_index(0);
    ASSERT_NOT_NULL(tip);
    ASSERT(strlen(tip) > 10);
    ASSERT_NULL(tips_get_by_index(-1));
    ASSERT_NULL(tips_get_by_index(tips_total()));
    return 0;
}

static int test_tips_cold_start(void) {
    tips_init();
    ASSERT_NULL(tips_get_random());
    return 0;
}

static int test_tips_after_warmup(void) {
    tips_init();
    tips_get_random();
    tips_get_random();
    const char *tip = tips_get_random();
    ASSERT_NOT_NULL(tip);
    ASSERT(strlen(tip) > 10);
    return 0;
}

static int test_tips_variety(void) {
    tips_init();
    tips_get_random();
    const char *seen[10] = {NULL};
    int unique = 0;
    for (int i = 0; i < 30 && unique < 5; i++) {
        const char *tip = tips_get_random();
        if (!tip)
            continue;
        int dup = 0;
        for (int j = 0; j < unique; j++)
            if (seen[j] == tip) {
                dup = 1;
                break;
            }
        if (!dup && unique < 10)
            seen[unique++] = tip;
    }
    ASSERT(unique >= 2);
    return 0;
}

static int test_tips_all_valid(void) {
    int total = tips_total();
    for (int i = 0; i < total; i++) {
        const char *tip = tips_get_by_index(i);
        ASSERT_NOT_NULL(tip);
        ASSERT(strlen(tip) > 5);
    }
    return 0;
}

int test_tips_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tips");
    RUN_TEST(test_tips_init);
    RUN_TEST(test_tips_total);
    RUN_TEST(test_tips_get_by_index);
    RUN_TEST(test_tips_cold_start);
    RUN_TEST(test_tips_after_warmup);
    RUN_TEST(test_tips_variety);
    RUN_TEST(test_tips_all_valid);
    TEST_RETURN_FAILED();
}
