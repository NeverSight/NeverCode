#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

/* We need cJSON for building test configs */
typedef struct cJSON cJSON;
extern cJSON *cJSON_CreateObject(void);
extern cJSON *cJSON_AddStringToObject(cJSON *, const char *, const char *);
extern cJSON *cJSON_AddNumberToObject(cJSON *, const char *, double);
extern cJSON *cJSON_AddBoolToObject(cJSON *, const char *, int);
extern void cJSON_Delete(cJSON *);

extern void config_schema_init(void);
extern int config_schema_validate(const cJSON *config);
extern int config_schema_error_count(void);
extern const char *config_schema_error_path(int idx);
extern const char *config_schema_error_msg(int idx);
extern char *config_schema_format_errors(void);

static int test_valid_config(void) {
    cJSON *cfg = cJSON_CreateObject();
    cJSON_AddStringToObject(cfg, "model", "gpt-5.4");
    cJSON_AddNumberToObject(cfg, "max_tokens", 16384);
    cJSON_AddBoolToObject(cfg, "auto_compact", 1);

    int errors = config_schema_validate(cfg);
    ASSERT_INT_EQ(0, errors);

    cJSON_Delete(cfg);
    return 0;
}

static int test_unknown_field(void) {
    cJSON *cfg = cJSON_CreateObject();
    cJSON_AddStringToObject(cfg, "model", "gpt-5.4");
    cJSON_AddStringToObject(cfg, "mdoel", "typo");

    int errors = config_schema_validate(cfg);
    ASSERT_GT(errors, 0);
    ASSERT_NOT_NULL(config_schema_error_msg(0));
    ASSERT_STR_CONTAINS(config_schema_error_msg(0), "did you mean");

    cJSON_Delete(cfg);
    return 0;
}

static int test_wrong_type(void) {
    cJSON *cfg = cJSON_CreateObject();
    cJSON_AddStringToObject(cfg, "max_tokens", "not-a-number");

    int errors = config_schema_validate(cfg);
    ASSERT_GT(errors, 0);
    ASSERT_STR_CONTAINS(config_schema_error_msg(0), "number");

    cJSON_Delete(cfg);
    return 0;
}

static int test_null_config(void) {
    int errors = config_schema_validate(NULL);
    ASSERT_GT(errors, 0);
    ASSERT_STR_CONTAINS(config_schema_error_msg(0), "root");
    return 0;
}

static int test_empty_config_ok(void) {
    cJSON *cfg = cJSON_CreateObject();
    int errors = config_schema_validate(cfg);
    ASSERT_INT_EQ(0, errors);
    cJSON_Delete(cfg);
    return 0;
}

static int test_format_errors(void) {
    cJSON *cfg = cJSON_CreateObject();
    cJSON_AddStringToObject(cfg, "bogus_field", "value");
    config_schema_validate(cfg);

    char *fmt = config_schema_format_errors();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "issue");
    free(fmt);
    cJSON_Delete(cfg);
    return 0;
}

static int test_format_no_errors(void) {
    config_schema_init();
    char *fmt = config_schema_format_errors();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "no errors");
    free(fmt);
    return 0;
}

static int test_fuzzy_macros(void) {
    ASSERT(SCHEMA_FUZZY_MAX_LEN > 0);
    ASSERT(SCHEMA_FUZZY_MAX_DIST > 0);
    ASSERT(SCHEMA_FUZZY_MAX_DIST <= SCHEMA_FUZZY_MAX_LEN);
    return 0;
}

int test_config_schema_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("config_schema");
    RUN_TEST(test_valid_config);
    RUN_TEST(test_unknown_field);
    RUN_TEST(test_wrong_type);
    RUN_TEST(test_null_config);
    RUN_TEST(test_empty_config_ok);
    RUN_TEST(test_format_errors);
    RUN_TEST(test_format_no_errors);
    RUN_TEST(test_fuzzy_macros);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("config_schema");
    RUN_TEST(test_valid_config);
    RUN_TEST(test_unknown_field);
    RUN_TEST(test_wrong_type);
    RUN_TEST(test_null_config);
    RUN_TEST(test_empty_config_ok);
    RUN_TEST(test_format_errors);
    RUN_TEST(test_format_no_errors);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
