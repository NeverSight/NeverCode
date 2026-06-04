#include "test.h"
#include "tools.h"
#include "nevercode.h"
#include <stdio.h>
#include <unistd.h>

static void write_test_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    fputs(content, f);
    fclose(f);
}

static int test_edit_shows_diff(void) {
    const char *path = "/tmp/nevercode_test_diff_edit.txt";
    write_test_file(path, "line1\nold_value\nline3\n");

    tools_register_all();
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    cJSON_AddStringToObject(input, "old_string", "old_value");
    cJSON_AddStringToObject(input, "new_string", "new_value");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "Replaced 1") != NULL);
    ASSERT(strstr(output, "--- a/") != NULL);
    ASSERT(strstr(output, "+++ b/") != NULL);
    ASSERT(strstr(output, "-old_value") != NULL);
    ASSERT(strstr(output, "+new_value") != NULL);

    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

static int test_edit_multiline_diff(void) {
    const char *path = "/tmp/nevercode_test_diff_multi.txt";
    write_test_file(path, "header\nalpha\nbeta\nfooter\n");

    const tool_def_t *t = tools_find("Edit");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    cJSON_AddStringToObject(input, "old_string", "alpha\nbeta");
    cJSON_AddStringToObject(input, "new_string", "gamma\ndelta\nepsilon");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "-alpha") != NULL);
    ASSERT(strstr(output, "-beta") != NULL);
    ASSERT(strstr(output, "+gamma") != NULL);
    ASSERT(strstr(output, "+delta") != NULL);
    ASSERT(strstr(output, "+epsilon") != NULL);

    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

static int test_edit_replace_all_diff(void) {
    const char *path = "/tmp/nevercode_test_diff_all.txt";
    write_test_file(path, "foo bar foo baz foo\n");

    const tool_def_t *t = tools_find("Edit");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    cJSON_AddStringToObject(input, "old_string", "foo");
    cJSON_AddStringToObject(input, "new_string", "qux");
    cJSON_AddBoolToObject(input, "replace_all", 1);

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Replaced 3") != NULL);
    ASSERT(strstr(output, "-foo") != NULL);
    ASSERT(strstr(output, "+qux") != NULL);

    free(output);
    cJSON_Delete(input);
    unlink(path);
    return 0;
}

static int test_edit_verify_file_content(void) {
    const char *path = "/tmp/nevercode_test_diff_verify.txt";
    write_test_file(path, "int x = 1;\nint y = 2;\n");

    const tool_def_t *t = tools_find("Edit");

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    cJSON_AddStringToObject(input, "old_string", "int x = 1;");
    cJSON_AddStringToObject(input, "new_string", "int x = 42;");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    free(output);
    cJSON_Delete(input);

    FILE *f = fopen(path, "r");
    char buf[256];
    fgets(buf, sizeof(buf), f);
    fclose(f);
    ASSERT(strstr(buf, "int x = 42;") != NULL);
    unlink(path);
    return 0;
}

int test_file_edit_diff_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("file_edit_diff");
    RUN_TEST(test_edit_shows_diff);
    RUN_TEST(test_edit_multiline_diff);
    RUN_TEST(test_edit_replace_all_diff);
    RUN_TEST(test_edit_verify_file_content);
    TEST_RETURN_FAILED();
}
