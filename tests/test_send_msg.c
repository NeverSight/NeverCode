#include "test.h"
#include "nevercode.h"

static int test_send_msg_bad_args(void) {
    char *out = NULL;
    ASSERT_INT_EQ(send_msg_exec(NULL, &out, NULL), -1);
    cJSON *in = cJSON_CreateObject();
    ASSERT_INT_EQ(send_msg_exec(in, &out, NULL), 1);
    cJSON_Delete(in);
    ASSERT_NOT_NULL(out);
    free(out);
    return 0;
}

static int test_send_msg_unknown_agent(void) {
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "agent_id", "nonexistent-agent-xyz");
    cJSON_AddStringToObject(in, "message", "hi");
    char *out = NULL;
    int rc = send_msg_exec(in, &out, NULL);
    cJSON_Delete(in);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(out);
    free(out);
    return 0;
}

int test_send_msg_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("send_msg");
    RUN_TEST(test_send_msg_bad_args);
    RUN_TEST(test_send_msg_unknown_agent);
    TEST_RETURN_FAILED();
}
