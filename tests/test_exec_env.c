#include "test.h"
#include "nevercode.h"
#include "defaults.h"
#include <string.h>
#include <stdlib.h>

static int test_exec_env_pair_count(void) {
    int n = exec_env_pair_count();
    ASSERT(n > 0);
    ASSERT(n <= EXEC_ENV_MAX_PAIRS);
    return 0;
}

static int test_exec_env_keys_values(void) {
    int n = exec_env_pair_count();
    for (int i = 0; i < n; i++) {
        ASSERT_NOT_NULL(exec_env_key_at(i));
        ASSERT_NOT_NULL(exec_env_value_at(i));
    }
    ASSERT_NULL(exec_env_key_at(-1));
    ASSERT_NULL(exec_env_key_at(n));
    return 0;
}

static int test_exec_env_no_color(void) {
    bool found = false;
    int n = exec_env_pair_count();
    for (int i = 0; i < n; i++) {
        if (strcmp(exec_env_key_at(i), "NO_COLOR") == 0) {
            ASSERT_STR_EQ(exec_env_value_at(i), "1");
            found = true;
        }
    }
    ASSERT_TRUE(found);
    return 0;
}

static int test_exec_env_build_array(void) {
    char *envp[20];
    int count = 0;
    exec_env_set_active(true);
    exec_env_build_array(envp, 20, &count);
    ASSERT(count > 0);

    bool found_pager = false;
    for (int i = 0; i < count; i++) {
        if (strstr(envp[i], "PAGER=cat"))
            found_pager = true;
    }
    ASSERT_TRUE(found_pager);

    exec_env_free_array(envp, count);
    return 0;
}

static int test_exec_env_toggle(void) {
    exec_env_set_active(false);
    ASSERT_FALSE(exec_env_is_active());

    char *envp[20];
    int count = 0;
    exec_env_build_array(envp, 20, &count);
    ASSERT_INT_EQ(count, 0);

    exec_env_set_active(true);
    ASSERT_TRUE(exec_env_is_active());
    return 0;
}

int test_exec_env_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("exec_env");

    RUN_TEST(test_exec_env_pair_count);
    RUN_TEST(test_exec_env_keys_values);
    RUN_TEST(test_exec_env_no_color);
    RUN_TEST(test_exec_env_build_array);
    RUN_TEST(test_exec_env_toggle);

    TEST_RETURN_FAILED();
}
