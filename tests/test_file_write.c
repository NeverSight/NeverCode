#include "test.h"
#include "tools.h"
#include <stdio.h>
#include <unistd.h>

static int test_register(void) {
    tool_file_write_register();
    const tool_def_t *t = tools_find("Write");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("Write");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_FALSE(t->read_only);
    return 0;
}

static int test_has_execute(void) {
    const tool_def_t *t = tools_find("Write");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->execute);
    return 0;
}

static int test_write_missing_content(void) {
    const tool_def_t *t = tools_find("Write");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "file_path", "/tmp/_nc_test_no_content");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_TRUE(rc != 0 || output != NULL);
    free(output);
    cJSON_Delete(input);
    unlink("/tmp/_nc_test_no_content");
    return 0;
}

int test_file_write_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("file_write");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_has_execute);
    RUN_TEST(test_write_missing_content);
    TEST_RETURN_FAILED();
}
