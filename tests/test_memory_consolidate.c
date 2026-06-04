#include "test.h"
#include "nevercode.h"
#include <string.h>

static int test_consolidate_noop_when_few_entries(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "user_name", "Alice");
    memory_set(&m, "auto_project_uses_", "Python");
    int merged = memory_consolidate(&m);
    ASSERT_INT_EQ(merged, 0);
    memory_free(&m);
    return 0;
}

static int test_consolidate_merges_same_category(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "auto_project_uses_react", "Uses React");
    memory_set(&m, "auto_project_uses_ts", "Uses TypeScript");
    memory_set(&m, "auto_stack_is_node", "Stack is Node");
    memory_set(&m, "auto_stack_is_express", "Stack is Express");
    memory_set(&m, "auto_db_is_postgres", "Database is Postgres");
    memory_set(&m, "auto_db_is_redis", "Cache is Redis");
    memory_set(&m, "manual_note", "Some note");

    int merged = memory_consolidate(&m);
    ASSERT(merged > 0);

    ASSERT_NOT_NULL(memory_get(&m, "auto_project"));
    const char *proj = memory_get(&m, "auto_project");
    ASSERT(strstr(proj, "React") != NULL);
    ASSERT(strstr(proj, "TypeScript") != NULL);

    ASSERT_NOT_NULL(memory_get(&m, "auto_stack"));
    ASSERT_NOT_NULL(memory_get(&m, "auto_db"));
    ASSERT_NOT_NULL(memory_get(&m, "manual_note"));

    memory_free(&m);
    return 0;
}

static int test_consolidate_preserves_non_auto(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "user_pref_theme", "dark");
    memory_set(&m, "auto_a_x", "v1");
    memory_set(&m, "auto_a_y", "v2");
    memory_set(&m, "auto_b_x", "v3");
    memory_set(&m, "auto_b_y", "v4");
    memory_set(&m, "auto_c_x", "v5");
    memory_set(&m, "auto_c_y", "v6");

    memory_consolidate(&m);
    ASSERT_NOT_NULL(memory_get(&m, "user_pref_theme"));
    ASSERT_STR_EQ(memory_get(&m, "user_pref_theme"), "dark");
    memory_free(&m);
    return 0;
}

static int test_memory_delete(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "a", "1");
    memory_set(&m, "b", "2");
    memory_set(&m, "c", "3");
    ASSERT_INT_EQ(memory_count(&m), 3);

    int rc = memory_delete(&m, "b");
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(memory_count(&m), 2);
    ASSERT_NULL(memory_get(&m, "b"));
    ASSERT_NOT_NULL(memory_get(&m, "a"));
    ASSERT_NOT_NULL(memory_get(&m, "c"));

    rc = memory_delete(&m, "nonexistent");
    ASSERT_INT_EQ(rc, -1);
    ASSERT_INT_EQ(memory_count(&m), 2);

    memory_free(&m);
    return 0;
}

static int test_memory_count(void) {
    memory_ctx_t m;
    memory_init(&m);
    ASSERT_INT_EQ(memory_count(&m), 0);
    memory_set(&m, "x", "y");
    ASSERT_INT_EQ(memory_count(&m), 1);
    memory_set(&m, "a", "b");
    ASSERT_INT_EQ(memory_count(&m), 2);
    memory_set(&m, "x", "z");
    ASSERT_INT_EQ(memory_count(&m), 2);
    memory_free(&m);
    return 0;
}

int test_memory_consolidate_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("memory_consolidate");
    RUN_TEST(test_consolidate_noop_when_few_entries);
    RUN_TEST(test_consolidate_merges_same_category);
    RUN_TEST(test_consolidate_preserves_non_auto);
    RUN_TEST(test_memory_delete);
    RUN_TEST(test_memory_count);
    TEST_RETURN_FAILED();
}
