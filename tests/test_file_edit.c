#include "test.h"
#include "tools.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>

static int test_register(void) {
    tool_file_edit_register();
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    return 0;
}

static int test_schema(void) {
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    ASSERT_NOT_NULL(t->description);
    return 0;
}

static int test_exec_missing_path(void) {
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "old_string", "foo");
    cJSON_AddStringToObject(input, "new_string", "bar");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_exec_file_not_found(void) {
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "file_path", "/tmp/_nevercode_test_nonexistent_xyzzy");
    cJSON_AddStringToObject(input, "old_string", "foo");
    cJSON_AddStringToObject(input, "new_string", "bar");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static const char *FUZZY_TMPFILE = "/tmp/_nc_test_fuzzy_edit.txt";

static void write_tmp(const char *content) {
    FILE *f = fopen(FUZZY_TMPFILE, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static char *read_tmp(void) {
    FILE *f = fopen(FUZZY_TMPFILE, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    sz = fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

static int test_exact_replace(void) {
    write_tmp("hello world\n");
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", FUZZY_TMPFILE);
    cJSON_AddStringToObject(input, "old_string", "hello");
    cJSON_AddStringToObject(input, "new_string", "goodbye");
    char *out = NULL;
    int rc = t->execute(input, &out, NULL);
    ASSERT_INT_EQ(rc, 0);
    char *content = read_tmp();
    ASSERT_NOT_NULL(content);
    ASSERT_NOT_NULL(strstr(content, "goodbye world"));
    free(content);
    free(out);
    cJSON_Delete(input);
    unlink(FUZZY_TMPFILE);
    return 0;
}

static int test_fuzzy_rstrip(void) {
    write_tmp("line one  \nline two\n");
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", FUZZY_TMPFILE);
    cJSON_AddStringToObject(input, "old_string", "line one\nline two");
    cJSON_AddStringToObject(input, "new_string", "replaced");
    char *out = NULL;
    int rc = t->execute(input, &out, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    char *content = read_tmp();
    ASSERT_NOT_NULL(content);
    ASSERT_NOT_NULL(strstr(content, "replaced"));
    ASSERT_NULL(strstr(content, "line one"));
    free(content);
    free(out);
    cJSON_Delete(input);
    unlink(FUZZY_TMPFILE);
    return 0;
}

static int test_fuzzy_indent_flex(void) {
    write_tmp("    indented code\n      more indent\n");
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", FUZZY_TMPFILE);
    cJSON_AddStringToObject(input, "old_string", "indented code\nmore indent");
    cJSON_AddStringToObject(input, "new_string", "fixed code\naligned");
    char *out = NULL;
    int rc = t->execute(input, &out, NULL);
    ASSERT_INT_EQ(rc, 0);
    char *content = read_tmp();
    ASSERT_NOT_NULL(content);
    ASSERT_NOT_NULL(strstr(content, "fixed code"));
    free(content);
    free(out);
    cJSON_Delete(input);
    unlink(FUZZY_TMPFILE);
    return 0;
}

static int test_replace_all_exact(void) {
    write_tmp("foo bar foo baz foo\n");
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", FUZZY_TMPFILE);
    cJSON_AddStringToObject(input, "old_string", "foo");
    cJSON_AddStringToObject(input, "new_string", "qux");
    cJSON_AddBoolToObject(input, "replace_all", 1);
    char *out = NULL;
    int rc = t->execute(input, &out, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_NOT_NULL(strstr(out, "3 occurrence"));
    char *content = read_tmp();
    ASSERT_NOT_NULL(content);
    ASSERT_NULL(strstr(content, "foo"));
    free(content);
    free(out);
    cJSON_Delete(input);
    unlink(FUZZY_TMPFILE);
    return 0;
}

static int test_not_unique_error(void) {
    write_tmp("aaa bbb aaa\n");
    const tool_def_t *t = tools_find("Edit");
    ASSERT_NOT_NULL(t);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", FUZZY_TMPFILE);
    cJSON_AddStringToObject(input, "old_string", "aaa");
    cJSON_AddStringToObject(input, "new_string", "zzz");
    char *out = NULL;
    int rc = t->execute(input, &out, NULL);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(out);
    ASSERT_NOT_NULL(strstr(out, "not unique"));
    free(out);
    cJSON_Delete(input);
    unlink(FUZZY_TMPFILE);
    return 0;
}

int test_file_edit_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("file_edit");
    RUN_TEST(test_register);
    RUN_TEST(test_schema);
    RUN_TEST(test_exec_missing_path);
    RUN_TEST(test_exec_file_not_found);
    RUN_TEST(test_exact_replace);
    RUN_TEST(test_fuzzy_rstrip);
    RUN_TEST(test_fuzzy_indent_flex);
    RUN_TEST(test_replace_all_exact);
    RUN_TEST(test_not_unique_error);
    TEST_RETURN_FAILED();
}
