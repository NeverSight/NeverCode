#include "test.h"
#include "tools.h"
#include "nevercode.h"

extern void tool_ask_user_register(void);

static int test_ask_user_schema(void) {
    const tool_def_t *t = tools_find("AskUserQuestion");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);
    ASSERT(t->destructive == false);
    ASSERT_NOT_NULL(t->input_schema);
    cJSON *props = cJSON_GetObjectItem(t->input_schema, "properties");
    ASSERT_NOT_NULL(props);
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "questions"));
    return 0;
}

static int test_ask_user_missing_questions(void) {
    const tool_def_t *t = tools_find("AskUserQuestion");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "questions") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_ask_user_empty_questions(void) {
    const tool_def_t *t = tools_find("AskUserQuestion");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddItemToObject(input, "questions", cJSON_CreateArray());
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT(rc != 0);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_ask_user_no_valid_questions(void) {
    const tool_def_t *t = tools_find("AskUserQuestion");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON *questions = cJSON_CreateArray();
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "question", "pick one?");
    cJSON *options = cJSON_CreateArray();
    cJSON *opt = cJSON_CreateObject();
    cJSON_AddStringToObject(opt, "label", "only");
    cJSON_AddItemToArray(options, opt); /* invalid: only 1 option */
    cJSON_AddItemToObject(q, "options", options);
    cJSON_AddItemToArray(questions, q);
    cJSON_AddItemToObject(input, "questions", questions);

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "no valid questions") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_ask_user_description(void) {
    const tool_def_t *t = tools_find("AskUserQuestion");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->description);
    ASSERT(strstr(t->description, "multiple-choice") != NULL);
    return 0;
}

static int test_ask_user_tool_name(void) {
    const tool_def_t *t = tools_find("AskUserQuestion");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t->name, "AskUserQuestion");
    return 0;
}

int test_ask_user_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("ask_user");
    RUN_TEST(test_ask_user_schema);
    RUN_TEST(test_ask_user_missing_questions);
    RUN_TEST(test_ask_user_empty_questions);
    RUN_TEST(test_ask_user_no_valid_questions);
    RUN_TEST(test_ask_user_description);
    RUN_TEST(test_ask_user_tool_name);
    TEST_RETURN_FAILED();
}
