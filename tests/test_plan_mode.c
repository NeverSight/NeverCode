/*
 * test_plan_mode.c — Tests for collaboration mode tools.
 */

#include "test.h"
#include "tools.h"
#include "nevercode.h"
#include <stdio.h>

extern bool g_plan_mode;
extern void session_mode_set(int mode);
extern int session_mode_get(void);

#define SMODE_DEFAULT 0
#define SMODE_PLAN 2
#define SMODE_EXECUTE 3
#define SMODE_PAIR 4

static int test_enter_plan_mode(void) {
    tools_register_all();
    g_plan_mode = false;
    session_mode_set(SMODE_DEFAULT);

    const tool_def_t *t = tools_find("EnterPlanMode");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "plan mode") != NULL);
    ASSERT(g_plan_mode == true);
    ASSERT_INT_EQ(session_mode_get(), SMODE_PLAN);
    free(output);

    g_plan_mode = false;
    session_mode_set(SMODE_DEFAULT);
    return 0;
}

static int test_enter_plan_mode_already_in(void) {
    g_plan_mode = true;
    session_mode_set(SMODE_PLAN);

    const tool_def_t *t = tools_find("EnterPlanMode");
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Already") != NULL);
    free(output);

    g_plan_mode = false;
    session_mode_set(SMODE_DEFAULT);
    return 0;
}

static int test_exit_plan_mode(void) {
    g_plan_mode = true;
    session_mode_set(SMODE_PLAN);

    const tool_def_t *t = tools_find("ExitPlanMode");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "plan_summary", "Refactor auth module");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Exited plan mode") != NULL);
    ASSERT(strstr(output, "Refactor auth module") != NULL);
    ASSERT(g_plan_mode == false);
    ASSERT_INT_EQ(session_mode_get(), SMODE_DEFAULT);
    free(output);
    return 0;
}

static int test_exit_plan_mode_not_in(void) {
    g_plan_mode = false;
    session_mode_set(SMODE_DEFAULT);

    const tool_def_t *t = tools_find("ExitPlanMode");
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Not in plan mode") != NULL);
    free(output);
    return 0;
}

static int test_enter_execute_mode(void) {
    session_mode_set(SMODE_DEFAULT);
    g_plan_mode = false;

    const tool_def_t *t = tools_find("EnterExecuteMode");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == false);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "execute mode") != NULL);
    ASSERT(strstr(output, "assumptions") != NULL);
    ASSERT_INT_EQ(session_mode_get(), SMODE_EXECUTE);
    ASSERT(g_plan_mode == false);
    free(output);

    session_mode_set(SMODE_DEFAULT);
    return 0;
}

static int test_enter_pair_mode(void) {
    session_mode_set(SMODE_DEFAULT);
    g_plan_mode = false;

    const tool_def_t *t = tools_find("EnterPairMode");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "pair") != NULL);
    ASSERT_INT_EQ(session_mode_get(), SMODE_PAIR);
    free(output);

    session_mode_set(SMODE_DEFAULT);
    return 0;
}

static int test_reset_mode(void) {
    session_mode_set(SMODE_EXECUTE);
    g_plan_mode = false;

    const tool_def_t *t = tools_find("ResetMode");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "default mode") != NULL);
    ASSERT(strstr(output, "execute") != NULL);
    ASSERT_INT_EQ(session_mode_get(), SMODE_DEFAULT);
    free(output);
    return 0;
}

static int test_mode_switch_chain(void) {
    session_mode_set(SMODE_DEFAULT);
    g_plan_mode = false;

    /* default -> plan -> exit -> execute -> reset */
    const tool_def_t *enter_plan = tools_find("EnterPlanMode");
    const tool_def_t *exit_plan = tools_find("ExitPlanMode");
    const tool_def_t *enter_exec = tools_find("EnterExecuteMode");
    const tool_def_t *reset = tools_find("ResetMode");

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;

    enter_plan->execute(input, &output, NULL);
    ASSERT_INT_EQ(session_mode_get(), SMODE_PLAN);
    ASSERT(g_plan_mode == true);
    free(output);
    output = NULL;

    exit_plan->execute(input, &output, NULL);
    ASSERT_INT_EQ(session_mode_get(), SMODE_DEFAULT);
    ASSERT(g_plan_mode == false);
    free(output);
    output = NULL;

    enter_exec->execute(input, &output, NULL);
    ASSERT_INT_EQ(session_mode_get(), SMODE_EXECUTE);
    free(output);
    output = NULL;

    reset->execute(input, &output, NULL);
    ASSERT_INT_EQ(session_mode_get(), SMODE_DEFAULT);
    free(output);

    cJSON_Delete(input);
    return 0;
}

int test_plan_mode_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("plan_mode");

    RUN_TEST(test_enter_plan_mode);
    RUN_TEST(test_enter_plan_mode_already_in);
    RUN_TEST(test_exit_plan_mode);
    RUN_TEST(test_exit_plan_mode_not_in);
    RUN_TEST(test_enter_execute_mode);
    RUN_TEST(test_enter_pair_mode);
    RUN_TEST(test_reset_mode);
    RUN_TEST(test_mode_switch_chain);

    TEST_RETURN_FAILED();
}
