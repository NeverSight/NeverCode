#include "test.h"
#include "nevercode.h"

static int test_ac_unknown(void) {
    ASSERT_INT_EQ(agent_control_pause("no-such-id"), -1);
    ASSERT_INT_EQ(agent_control_resume("no-such-id"), -1);
    ASSERT_INT_EQ(agent_control_cancel("no-such-id"), -1);
    return 0;
}

static int test_ac_exec_schema(void) {
    cJSON *in = cJSON_CreateObject();
    char *out = NULL;
    ASSERT_INT_EQ(agent_control_exec(in, &out, NULL), 1);
    cJSON_Delete(in);
    free(out);

    in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "agent_id", "x");
    cJSON_AddStringToObject(in, "action", "pause");
    out = NULL;
    int rc = agent_control_exec(in, &out, NULL);
    cJSON_Delete(in);
    ASSERT(rc == 0 || rc == 1);
    free(out);
    return 0;
}

static int test_status_name(void) {
    ASSERT_STR_EQ(agent_status_name(AGENT_STATUS_RUNNING), "running");
    ASSERT_STR_EQ(agent_status_name(AGENT_STATUS_PAUSED), "paused");
    return 0;
}

int test_agent_control_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_control");
    RUN_TEST(test_ac_unknown);
    RUN_TEST(test_ac_exec_schema);
    RUN_TEST(test_status_name);
    TEST_RETURN_FAILED();
}
