/*
 * test_risk_engine.c
 */

#include "test.h"
#include "nevercode.h"
#include <stdlib.h>

static int test_levels(void) {
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "command", "echo hello");
    ASSERT_INT_EQ((int)risk_assess_tool("file_read", in), (int)RISK_LOW);
    cJSON_Delete(in);

    in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "command", "rm -rf /tmp/x");
    risk_level_t lv = risk_assess_tool("bash", in);
    ASSERT_INT_EQ((int)lv, (int)RISK_LOW);
    cJSON_Delete(in);
    return 0;
}

static int test_path_score(void) {
    ASSERT(risk_score_path("/etc/passwd") > 0);
    ASSERT(risk_score_path("/home/u/.ssh/id_rsa") > 0);
    return 0;
}

static int test_describe(void) {
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "command", "ls");
    char *d = risk_assess_describe("bash", in);
    ASSERT_NOT_NULL(d);
    ASSERT(strstr(d, "bash") != NULL);
    free(d);
    cJSON_Delete(in);
    return 0;
}

int test_risk_engine_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("risk_engine");
    RUN_TEST(test_levels);
    RUN_TEST(test_path_score);
    RUN_TEST(test_describe);
    TEST_RETURN_FAILED();
}
