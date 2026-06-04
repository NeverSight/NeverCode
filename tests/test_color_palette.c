#include "test.h"
#include "nevercode.h"
#include "defaults.h"

static int test_init_free(void) {
    palette_init();
    ASSERT_GE(palette_capability(), 0);
    palette_free();
    return 0;
}

static int test_seq_returns_string(void) {
    palette_init();
    for (int r = 0; r < PAL_ROLE_COUNT; r++) {
        const char *s = palette_seq(r);
        ASSERT_NOT_NULL(s);
    }
    palette_free();
    return 0;
}

static int test_reset(void) {
    palette_init();
    palette_force_cap(1);
    const char *r = palette_reset();
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "\033[0m");
    palette_free();
    return 0;
}

static int test_force_cap(void) {
    palette_init();
    palette_force_cap(0);
    ASSERT_INT_EQ(palette_capability(), 0);
    const char *s = palette_seq(PAL_ROLE_HEADER);
    ASSERT_NOT_NULL(s);
    ASSERT_INT_EQ((int)strlen(s), 0);

    palette_force_cap(1);
    ASSERT_INT_EQ(palette_capability(), 1);
    s = palette_seq(PAL_ROLE_HEADER);
    ASSERT(strlen(s) > 0);

    palette_free();
    return 0;
}

static int test_wrap(void) {
    palette_init();
    palette_force_cap(1);
    char *w = palette_wrap(PAL_ROLE_ERROR, "oops");
    ASSERT_NOT_NULL(w);
    ASSERT_STR_CONTAINS(w, "oops");
    ASSERT_STR_CONTAINS(w, "\033[");
    free(w);
    palette_free();
    return 0;
}

static int test_sprint(void) {
    palette_init();
    palette_force_cap(1);
    char buf[256];
    palette_sprint(buf, sizeof(buf), PAL_ROLE_SUCCESS, "ok");
    ASSERT_STR_CONTAINS(buf, "ok");
    ASSERT_STR_CONTAINS(buf, "\033[");

    palette_force_cap(0);
    palette_sprint(buf, sizeof(buf), PAL_ROLE_SUCCESS, "ok");
    ASSERT_STR_EQ(buf, "ok");

    palette_free();
    return 0;
}

static int test_null_safety(void) {
    palette_init();
    palette_sprint(NULL, 0, PAL_ROLE_HEADER, "x");
    char *w = palette_wrap(PAL_ROLE_HEADER, NULL);
    ASSERT_NOT_NULL(w);
    free(w);
    palette_free();
    return 0;
}

static int test_oob_role(void) {
    palette_init();
    const char *s = palette_seq(-1);
    ASSERT_NOT_NULL(s);
    s = palette_seq(999);
    ASSERT_NOT_NULL(s);
    palette_free();
    return 0;
}

int test_color_palette_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("color_palette");
    RUN_TEST(test_init_free);
    RUN_TEST(test_seq_returns_string);
    RUN_TEST(test_reset);
    RUN_TEST(test_force_cap);
    RUN_TEST(test_wrap);
    RUN_TEST(test_sprint);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_oob_role);
    TEST_RETURN_FAILED();
}
