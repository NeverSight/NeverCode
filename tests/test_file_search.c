#include "test.h"
#include "nevercode.h"

static int test_fs_missing_query(void) {
    cJSON *in = cJSON_CreateObject();
    char *out = NULL;
    int rc = file_search_exec(in, &out, NULL);
    cJSON_Delete(in);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(out);
    free(out);
    return 0;
}

static int test_fs_basic(void) {
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "query", "Makefile");
    char *out = NULL;
    int rc = file_search_exec(in, &out, NULL);
    cJSON_Delete(in);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "Found");
    free(out);
    return 0;
}

int test_file_search_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("file_search");
    RUN_TEST(test_fs_missing_query);
    RUN_TEST(test_fs_basic);
    TEST_RETURN_FAILED();
}
