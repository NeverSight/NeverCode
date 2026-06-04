#include "test.h"
#include "nevercode.h"
#include "../third_party/cJSON/cJSON.h"

typedef struct unified_exec_request_t unified_exec_request_t;
typedef struct unified_exec_result_t unified_exec_result_t;

void uexec_request_init(unified_exec_request_t *req);
void uexec_request_free(unified_exec_request_t *req);
void uexec_result_free(unified_exec_result_t *res);
int uexec_from_json(unified_exec_request_t *req, const cJSON *args);
int uexec_run(const unified_exec_request_t *req, unified_exec_result_t *res);
char *uexec_format_result(const unified_exec_result_t *res);

static char req_buf[512];
static char res_buf[512];

static int test_from_json(void) {
    unified_exec_request_t *req = (unified_exec_request_t *)req_buf;

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "cmd", "echo hello");
    cJSON_AddStringToObject(args, "workdir", "/tmp");
    cJSON_AddNumberToObject(args, "yield_time_ms", 5000);

    int r = uexec_from_json(req, args);
    ASSERT_INT_EQ(r, 0);

    cJSON_Delete(args);
    uexec_request_free(req);
    return 0;
}

static int test_from_json_no_cmd(void) {
    unified_exec_request_t *req = (unified_exec_request_t *)req_buf;

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "workdir", "/tmp");

    int r = uexec_from_json(req, args);
    ASSERT_INT_EQ(r, -1);

    cJSON_Delete(args);
    return 0;
}

static int test_from_json_null(void) {
    unified_exec_request_t *req = (unified_exec_request_t *)req_buf;
    ASSERT_INT_EQ(uexec_from_json(req, NULL), -1);
    ASSERT_INT_EQ(uexec_from_json(NULL, NULL), -1);
    return 0;
}

static int test_run_echo(void) {
    unified_exec_request_t *req = (unified_exec_request_t *)req_buf;
    unified_exec_result_t *res = (unified_exec_result_t *)res_buf;

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "cmd", "echo test_output");
    uexec_from_json(req, args);
    cJSON_Delete(args);

    int r = uexec_run(req, res);
    ASSERT_INT_EQ(r, 0);

    char *fmt = uexec_format_result(res);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "exit_code=0");
    ASSERT_STR_CONTAINS(fmt, "test_output");
    free(fmt);

    uexec_request_free(req);
    uexec_result_free(res);
    return 0;
}

static int test_run_failure(void) {
    unified_exec_request_t *req = (unified_exec_request_t *)req_buf;
    unified_exec_result_t *res = (unified_exec_result_t *)res_buf;

    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "cmd", "false");
    uexec_from_json(req, args);
    cJSON_Delete(args);

    uexec_run(req, res);
    char *fmt = uexec_format_result(res);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "exit_code=1");
    free(fmt);

    uexec_request_free(req);
    uexec_result_free(res);
    return 0;
}

static int test_format_null(void) {
    ASSERT_NULL(uexec_format_result(NULL));
    return 0;
}

int test_unified_exec_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("unified_exec");
    RUN_TEST(test_from_json);
    RUN_TEST(test_from_json_no_cmd);
    RUN_TEST(test_from_json_null);
    RUN_TEST(test_run_echo);
    RUN_TEST(test_run_failure);
    RUN_TEST(test_format_null);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("unified_exec");
    RUN_TEST(test_from_json);
    RUN_TEST(test_from_json_no_cmd);
    RUN_TEST(test_from_json_null);
    RUN_TEST(test_run_echo);
    RUN_TEST(test_run_failure);
    RUN_TEST(test_format_null);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
