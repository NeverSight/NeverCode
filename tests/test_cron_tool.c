#include "test.h"
#include "nevercode.h"
#include <unistd.h>
static int t1(void) {
    char id[64];
    ASSERT_INT_EQ(scheduler_add("t", "echo hi", 0, 0, 1, id, sizeof(id)), 0);
    ASSERT(strlen(id) > 0);
    return 0;
}
static int t2(void) {
    ASSERT(scheduler_active_count() >= 0);
    return 0;
}
static int t3(void) {
    ASSERT(scheduler_cancel("nonexistent-xyz") < 0);
    return 0;
}
static int t4(void) {
    char *l = scheduler_format_list();
    ASSERT_NOT_NULL(l);
    ASSERT_STR_CONTAINS(l, "Scheduled Tasks");
    free(l);
    return 0;
}
static int t5(void) {
    cJSON *i = cJSON_CreateObject();
    cJSON_AddStringToObject(i, "action", "schedule");
    cJSON_AddStringToObject(i, "command", "echo test");
    char *o = NULL;
    ASSERT_INT_EQ(cron_tool_exec(i, &o, NULL), 0);
    ASSERT_STR_CONTAINS(o, "Scheduled");
    free(o);
    cJSON_Delete(i);
    return 0;
}
static int t6(void) {
    cJSON *i = cJSON_CreateObject();
    cJSON_AddStringToObject(i, "action", "list");
    char *o = NULL;
    ASSERT_INT_EQ(cron_tool_exec(i, &o, NULL), 0);
    free(o);
    cJSON_Delete(i);
    return 0;
}
static int t7(void) {
    cJSON *i = cJSON_CreateObject();
    cJSON_AddStringToObject(i, "action", "schedule");
    char *o = NULL;
    ASSERT(cron_tool_exec(i, &o, NULL) != 0);
    free(o);
    cJSON_Delete(i);
    return 0;
}
static int t8(void) {
    cJSON *i = cJSON_CreateObject();
    cJSON_AddStringToObject(i, "action", "explode");
    char *o = NULL;
    ASSERT(cron_tool_exec(i, &o, NULL) != 0);
    free(o);
    cJSON_Delete(i);
    return 0;
}
static int t9(void) {
    char *o = NULL;
    ASSERT(cron_tool_exec(NULL, &o, NULL) != 0);
    free(o);
    return 0;
}
int test_cron_tool_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("cron_tool");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    TEST_RETURN_FAILED();
}
