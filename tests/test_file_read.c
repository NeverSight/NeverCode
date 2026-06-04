#include "test.h"
#include "tools.h"
#include <stdio.h>
#include <unistd.h>

static int test_register(void) {
    tool_file_read_register();
    const tool_def_t *t = tools_find("Read");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("Read");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_TRUE(t->read_only);
    return 0;
}

static int test_read_existing(void) {
    const tool_def_t *t = tools_find("Read");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->execute);
    return 0;
}

static int test_read_missing(void) {
    const tool_def_t *t = tools_find("Read");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "file_path", "/tmp/_nevercode_nonexistent_xyz");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_TRUE(rc != 0);
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_file_read_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("file_read");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_read_existing);
    RUN_TEST(test_read_missing);
    TEST_RETURN_FAILED();
}
