#include "test.h"
#include "tools.h"
#include <unistd.h>

static const char *tmppath = "/tmp/nc_test_multi_edit.txt";

static void write_tmp(const char *content) {
    FILE *f = fopen(tmppath, "w");
    if (!f)
        abort();
    fputs(content, f);
    fclose(f);
}

static char *read_tmp(void) {
    return strbuf_read_file(tmppath, NULL);
}

static int t_basic(void) {
    tools_register_all();
    write_tmp("hello world\nfoo bar\nbaz qux\n");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", tmppath);
    cJSON *edits = cJSON_AddArrayToObject(input, "edits");

    cJSON *e1 = cJSON_CreateObject();
    cJSON_AddStringToObject(e1, "old_string", "hello");
    cJSON_AddStringToObject(e1, "new_string", "HELLO");
    cJSON_AddItemToArray(edits, e1);

    cJSON *e2 = cJSON_CreateObject();
    cJSON_AddStringToObject(e2, "old_string", "foo");
    cJSON_AddStringToObject(e2, "new_string", "FOO");
    cJSON_AddItemToArray(edits, e2);

    char *output = NULL;
    const tool_def_t *tool = tools_find("MultiEdit");
    ASSERT_NOT_NULL(tool);
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT_NOT_NULL(strstr(output, "2/2 edits applied"));

    char *content = read_tmp();
    ASSERT_NOT_NULL(strstr(content, "HELLO world"));
    ASSERT_NOT_NULL(strstr(content, "FOO bar"));
    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(tmppath);
    return 0;
}

static int t_sequential(void) {
    write_tmp("aaa bbb ccc\n");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", tmppath);
    cJSON *edits = cJSON_AddArrayToObject(input, "edits");

    cJSON *e1 = cJSON_CreateObject();
    cJSON_AddStringToObject(e1, "old_string", "aaa");
    cJSON_AddStringToObject(e1, "new_string", "xxx");
    cJSON_AddItemToArray(edits, e1);

    cJSON *e2 = cJSON_CreateObject();
    cJSON_AddStringToObject(e2, "old_string", "xxx bbb");
    cJSON_AddStringToObject(e2, "new_string", "RESULT");
    cJSON_AddItemToArray(edits, e2);

    char *output = NULL;
    const tool_def_t *tool = tools_find("MultiEdit");
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);

    char *content = read_tmp();
    ASSERT_NOT_NULL(strstr(content, "RESULT"));
    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(tmppath);
    return 0;
}

static int t_partial_fail(void) {
    write_tmp("hello world\n");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", tmppath);
    cJSON *edits = cJSON_AddArrayToObject(input, "edits");

    cJSON *e1 = cJSON_CreateObject();
    cJSON_AddStringToObject(e1, "old_string", "hello");
    cJSON_AddStringToObject(e1, "new_string", "HI");
    cJSON_AddItemToArray(edits, e1);

    cJSON *e2 = cJSON_CreateObject();
    cJSON_AddStringToObject(e2, "old_string", "NONEXISTENT");
    cJSON_AddStringToObject(e2, "new_string", "X");
    cJSON_AddItemToArray(edits, e2);

    char *output = NULL;
    const tool_def_t *tool = tools_find("MultiEdit");
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(strstr(output, "1/2 edits applied"));
    ASSERT_NOT_NULL(strstr(output, "1 failed"));

    char *content = read_tmp();
    ASSERT_NOT_NULL(strstr(content, "HI world"));
    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(tmppath);
    return 0;
}

static int t_empty_edits(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", tmppath);
    cJSON_AddArrayToObject(input, "edits");

    char *output = NULL;
    const tool_def_t *tool = tools_find("MultiEdit");
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(strstr(output, "empty"));
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int t_replace_all(void) {
    write_tmp("aaa bbb aaa ccc aaa\n");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", tmppath);
    cJSON *edits = cJSON_AddArrayToObject(input, "edits");

    cJSON *e1 = cJSON_CreateObject();
    cJSON_AddStringToObject(e1, "old_string", "aaa");
    cJSON_AddStringToObject(e1, "new_string", "ZZZ");
    cJSON_AddBoolToObject(e1, "replace_all", 1);
    cJSON_AddItemToArray(edits, e1);

    char *output = NULL;
    const tool_def_t *tool = tools_find("MultiEdit");
    int rc = tool->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(strstr(output, "3 replacement(s)"));

    char *content = read_tmp();
    ASSERT_NOT_NULL(strstr(content, "ZZZ bbb ZZZ ccc ZZZ"));
    free(content);
    free(output);
    cJSON_Delete(input);
    unlink(tmppath);
    return 0;
}

int test_multi_edit_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("multi_edit");
    RUN_TEST(t_basic);
    RUN_TEST(t_sequential);
    RUN_TEST(t_partial_fail);
    RUN_TEST(t_empty_edits);
    RUN_TEST(t_replace_all);
    TEST_RETURN_FAILED();
}
