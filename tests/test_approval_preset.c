/*
 * test_approval_preset.c
 */
#include "test.h"
#include "nevercode.h"
#include "permission.h"

extern int security_presets_list(const security_preset_t **out, int max);
extern const security_preset_t *security_preset_find(const char *id);
extern int security_preset_count(void);
extern char *security_presets_format(void);

static int t_count(void) {
    int n = security_preset_count();
    ASSERT_INT_EQ(n, 3);
    return 0;
}

static int t_list(void) {
    const security_preset_t *items[8];
    int n = security_presets_list(items, 8);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(items[0]->id, "read-only");
    ASSERT_STR_EQ(items[1]->id, "standard");
    ASSERT_STR_EQ(items[2]->id, "full-access");
    return 0;
}

static int t_find(void) {
    const security_preset_t *p = security_preset_find("full-access");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->id, "full-access");
    ASSERT_INT_EQ(p->approval_value, APPROVAL_FULL);
    return 0;
}

static int t_find_null(void) {
    ASSERT_NULL(security_preset_find(NULL));
    ASSERT_NULL(security_preset_find("nonexistent"));
    return 0;
}

static int t_format(void) {
    char *fmt = security_presets_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "read-only");
    ASSERT_STR_CONTAINS(fmt, "full-access");
    ASSERT_STR_CONTAINS(fmt, "standard");
    free(fmt);
    return 0;
}

int test_approval_preset_all(void) {
    TEST_SUITE("approval_preset");
    TEST_COUNTERS;
    RUN_TEST(t_count);
    RUN_TEST(t_list);
    RUN_TEST(t_find);
    RUN_TEST(t_find_null);
    RUN_TEST(t_format);
    TEST_RETURN_FAILED();
}
