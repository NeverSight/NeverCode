#include "test.h"
#include "nevercode.h"

typedef struct eac_result_t eac_result_t;

int eac_detect(eac_result_t *result, const char *project_root, int include_home);
int eac_import(eac_result_t *result, const char *dest_base);
char *eac_format_detected(const eac_result_t *result);
int eac_detected_count(const eac_result_t *result);

static char result_buf[65536];

static int test_detect_null(void) {
    ASSERT_INT_EQ(eac_detect(NULL, "/tmp", 0), -1);
    return 0;
}

static int test_detect_no_configs(void) {
    eac_result_t *r = (eac_result_t *)result_buf;
    int n = eac_detect(r, "/tmp/nevercode_test_no_ext_configs", 0);
    ASSERT_INT_EQ(n, 0);
    ASSERT_INT_EQ(eac_detected_count(r), 0);
    return 0;
}

static int test_format_empty(void) {
    eac_result_t *r = (eac_result_t *)result_buf;
    eac_detect(r, "/tmp/nonexistent", 0);
    char *s = eac_format_detected(r);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "No external");
    free(s);
    return 0;
}

static int test_format_null(void) {
    char *s = eac_format_detected(NULL);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "No external");
    free(s);
    return 0;
}

static int test_import_null(void) {
    ASSERT_INT_EQ(eac_import(NULL, "/tmp"), -1);
    eac_result_t *r = (eac_result_t *)result_buf;
    ASSERT_INT_EQ(eac_import(r, NULL), -1);
    return 0;
}

int test_ext_agent_config_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("ext_agent_config");
    RUN_TEST(test_detect_null);
    RUN_TEST(test_detect_no_configs);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_format_null);
    RUN_TEST(test_import_null);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("ext_agent_config");
    RUN_TEST(test_detect_null);
    RUN_TEST(test_detect_no_configs);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_format_null);
    RUN_TEST(test_import_null);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
