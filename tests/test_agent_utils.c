#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <string.h>

static int test_get_self_path_not_null(void) {
    char *p = agent_get_self_path();
    ASSERT_NOT_NULL(p);
    ASSERT(strlen(p) > 0);
    return 0;
}

static int test_get_self_path_cached(void) {
    char *p1 = agent_get_self_path();
    char *p2 = agent_get_self_path();
    ASSERT(p1 == p2);
    return 0;
}

static int test_get_self_path_valid(void) {
    char *p = agent_get_self_path();
    ASSERT(strstr(p, NC_BINARY_NAME) != NULL || strstr(p, "test") != NULL);
    return 0;
}

int test_agent_utils_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_utils");

    RUN_TEST(test_get_self_path_not_null);
    RUN_TEST(test_get_self_path_cached);
    RUN_TEST(test_get_self_path_valid);

    TEST_RETURN_FAILED();
}
