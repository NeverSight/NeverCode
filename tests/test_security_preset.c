#include "test.h"
#include "nevercode.h"
#include "permission.h"
#include <stdlib.h>
#include <string.h>

static int test_preset_count(void) {
    ASSERT_INT_EQ(security_preset_count(), 3);
    return 0;
}

static int test_preset_list(void) {
    const security_preset_t *presets[8];
    int n = security_presets_list(presets, 8);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(presets[0]->id, "read-only");
    ASSERT_STR_EQ(presets[1]->id, "standard");
    ASSERT_STR_EQ(presets[2]->id, "full-access");
    return 0;
}

static int test_preset_find(void) {
    const security_preset_t *p = security_preset_find("standard");
    ASSERT(p != NULL);
    ASSERT_STR_EQ(p->id, "standard");
    ASSERT_STR_EQ(p->label, "Standard");
    ASSERT(p->approval_value == APPROVAL_DEFAULT);
    return 0;
}

static int test_preset_find_readonly(void) {
    const security_preset_t *p = security_preset_find("read-only");
    ASSERT(p != NULL);
    ASSERT(p->approval_value == APPROVAL_READONLY);
    ASSERT(strstr(p->description, "read") != NULL);
    return 0;
}

static int test_preset_find_full(void) {
    const security_preset_t *p = security_preset_find("full-access");
    ASSERT(p != NULL);
    ASSERT(p->approval_value == APPROVAL_FULL);
    ASSERT(strstr(p->description, "caution") != NULL);
    return 0;
}

static int test_preset_find_null(void) {
    ASSERT(security_preset_find(NULL) == NULL);
    ASSERT(security_preset_find("nonexistent") == NULL);
    return 0;
}

static int test_preset_format(void) {
    char *fmt = security_presets_format();
    ASSERT(fmt != NULL);
    ASSERT(strstr(fmt, "read-only") != NULL);
    ASSERT(strstr(fmt, "standard") != NULL);
    ASSERT(strstr(fmt, "full-access") != NULL);
    ASSERT(strstr(fmt, "Security Presets") != NULL);
    free(fmt);
    return 0;
}

static int test_preset_descriptions_non_empty(void) {
    const security_preset_t *presets[8];
    int n = security_presets_list(presets, 8);
    for (int i = 0; i < n; i++) {
        ASSERT(presets[i]->description != NULL);
        ASSERT(strlen(presets[i]->description) > 10);
        ASSERT(presets[i]->label != NULL);
        ASSERT(strlen(presets[i]->label) > 0);
    }
    return 0;
}

int test_security_preset_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("security_preset");
    RUN_TEST(test_preset_count);
    RUN_TEST(test_preset_list);
    RUN_TEST(test_preset_find);
    RUN_TEST(test_preset_find_readonly);
    RUN_TEST(test_preset_find_full);
    RUN_TEST(test_preset_find_null);
    RUN_TEST(test_preset_format);
    RUN_TEST(test_preset_descriptions_non_empty);
    TEST_RETURN_FAILED();
}
