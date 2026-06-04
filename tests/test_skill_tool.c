#include "test.h"
#include "tools.h"
#include "nevercode.h"
#include <stdio.h>

static int test_skill_tool_registered(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("Skill");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);
    ASSERT_NOT_NULL(t->description);
    return 0;
}

static int test_skill_verify(void) {
    const tool_def_t *t = tools_find("Skill");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "skill", "verify");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "verify") != NULL || strstr(output, "test") != NULL);
    free(output);
    return 0;
}

static int test_skill_debug(void) {
    const tool_def_t *t = tools_find("Skill");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "skill", "debug");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "debug") != NULL || strstr(output, "diagnostic") != NULL);
    free(output);
    return 0;
}

static int test_skill_with_args(void) {
    const tool_def_t *t = tools_find("Skill");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "skill", "test");
    cJSON_AddStringToObject(input, "args", "focus on edge cases in auth.c");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "focus on edge cases in auth.c") != NULL);
    free(output);
    return 0;
}

static int test_skill_with_slash_prefix(void) {
    const tool_def_t *t = tools_find("Skill");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "skill", "/verify");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "verify") != NULL || strstr(output, "test") != NULL);
    free(output);
    return 0;
}

static int test_skill_unknown(void) {
    const tool_def_t *t = tools_find("Skill");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "skill", "nonexistent_skill_xyz");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "Unknown skill") != NULL);
    ASSERT(strstr(output, "Available skills") != NULL);
    free(output);
    return 0;
}

static int test_skill_missing_param(void) {
    const tool_def_t *t = tools_find("Skill");
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "missing") != NULL);
    free(output);
    return 0;
}

int test_skill_tool_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("skill_tool");

    RUN_TEST(test_skill_tool_registered);
    RUN_TEST(test_skill_verify);
    RUN_TEST(test_skill_debug);
    RUN_TEST(test_skill_with_args);
    RUN_TEST(test_skill_with_slash_prefix);
    RUN_TEST(test_skill_unknown);
    RUN_TEST(test_skill_missing_param);

    TEST_RETURN_FAILED();
}
