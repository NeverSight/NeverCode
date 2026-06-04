#include "test.h"
#include "nevercode.h"

static int g_call_count;

static char *compute_hello(void *ud) {
    (void)ud;
    g_call_count++;
    return nc_xstrdup("Hello from section");
}

static char *compute_dynamic(void *ud) {
    (void)ud;
    g_call_count++;
    char buf[32];
    snprintf(buf, sizeof(buf), "call_%d", g_call_count);
    return nc_xstrdup(buf);
}

static char *compute_null(void *ud) {
    (void)ud;
    return NULL;
}

static int t_init_empty(void) {
    psec_init();
    ASSERT_INT_EQ(psec_count(), 0);
    return 0;
}

static int t_register_and_resolve(void) {
    psec_init();
    g_call_count = 0;
    ASSERT_INT_EQ(psec_register("greet", compute_hello, NULL, false), 0);
    ASSERT_INT_EQ(psec_count(), 1);
    const char *val = psec_resolve("greet");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_CONTAINS(val, "Hello");
    ASSERT_INT_EQ(g_call_count, 1);
    return 0;
}

static int t_memoization(void) {
    psec_init();
    g_call_count = 0;
    psec_register("memo", compute_hello, NULL, false);
    psec_resolve("memo");
    psec_resolve("memo");
    psec_resolve("memo");
    ASSERT_INT_EQ(g_call_count, 1);
    return 0;
}

static int t_volatile_recomputes(void) {
    psec_init();
    g_call_count = 0;
    psec_register("vol", compute_dynamic, NULL, true);
    ASSERT_TRUE(psec_is_volatile("vol"));
    const char *v1 = psec_resolve("vol");
    ASSERT_NOT_NULL(v1);
    const char *v2 = psec_resolve("vol");
    ASSERT_NOT_NULL(v2);
    ASSERT_INT_EQ(g_call_count, 2);
    return 0;
}

static int t_invalidate(void) {
    psec_init();
    g_call_count = 0;
    psec_register("inv", compute_hello, NULL, false);
    psec_resolve("inv");
    ASSERT_INT_EQ(g_call_count, 1);
    psec_invalidate("inv");
    psec_resolve("inv");
    ASSERT_INT_EQ(g_call_count, 2);
    return 0;
}

static int t_invalidate_all(void) {
    psec_init();
    g_call_count = 0;
    psec_register("a", compute_hello, NULL, false);
    psec_register("b", compute_hello, NULL, false);
    psec_resolve("a");
    psec_resolve("b");
    ASSERT_INT_EQ(g_call_count, 2);
    psec_invalidate_all();
    psec_resolve("a");
    psec_resolve("b");
    ASSERT_INT_EQ(g_call_count, 4);
    return 0;
}

static int t_resolve_unknown(void) {
    psec_init();
    ASSERT_NULL(psec_resolve("nonexistent"));
    ASSERT_NULL(psec_resolve(NULL));
    return 0;
}

static int t_resolve_all(void) {
    psec_init();
    psec_register("x", compute_hello, NULL, false);
    psec_register("y", compute_null, NULL, false);
    psec_register("z", compute_hello, NULL, false);
    char *parts[8];
    int n = psec_resolve_all(parts, 8);
    ASSERT_INT_EQ(n, 2);
    return 0;
}

static int t_duplicate_name(void) {
    psec_init();
    ASSERT_INT_EQ(psec_register("dup", compute_hello, NULL, false), 0);
    ASSERT_INT_EQ(psec_register("dup", compute_hello, NULL, false), -1);
    ASSERT_INT_EQ(psec_count(), 1);
    return 0;
}

static int t_macros(void) {
    ASSERT_GT(PSEC_MAX_SECTIONS, 0);
    ASSERT_GT(PSEC_NAME_MAX, 0);
    return 0;
}

int test_prompt_section_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("prompt_section");
    RUN_TEST(t_init_empty);
    RUN_TEST(t_register_and_resolve);
    RUN_TEST(t_memoization);
    RUN_TEST(t_volatile_recomputes);
    RUN_TEST(t_invalidate);
    RUN_TEST(t_invalidate_all);
    RUN_TEST(t_resolve_unknown);
    RUN_TEST(t_resolve_all);
    RUN_TEST(t_duplicate_name);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
