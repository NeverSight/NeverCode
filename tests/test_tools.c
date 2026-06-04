#include "test.h"
#include "tools.h"
#include "nevercode.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

static int test_tool_registry(void) {
    tools_register_all();
    ASSERT(tools_count() >= 10);

    const tool_def_t *bash = tools_find("Bash");
    ASSERT_NOT_NULL(bash);
    ASSERT_STR_EQ(bash->name, "Bash");

    const tool_def_t *read = tools_find("Read");
    ASSERT_NOT_NULL(read);

    const tool_def_t *nope = tools_find("NonExistentTool");
    ASSERT_NULL(nope);
    return 0;
}

static int test_tool_all_registered(void) {
    const char *expected[] = {"Bash", "Read",  "Write",    "Edit",      "Glob",
                              "Grep", "Agent", "WebFetch", "WebSearch", "TodoWrite"};
    for (int i = 0; i < 10; i++) {
        const tool_def_t *t = tools_find(expected[i]);
        ASSERT_NOT_NULL(t);
        ASSERT_NOT_NULL(t->description);
        ASSERT_NOT_NULL(t->input_schema);
        ASSERT_NOT_NULL(t->execute);
    }
    return 0;
}

static int test_bash_echo(void) {
    const tool_def_t *bash = tools_find("Bash");
    ASSERT_NOT_NULL(bash);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "echo 'hello test'");
    char *output = NULL;
    int rc = bash->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "hello test") != NULL);
    ASSERT(strstr(output, "[exit: 0]") != NULL);
    free(output);
    return 0;
}

static int test_bash_exit_code(void) {
    const tool_def_t *bash = tools_find("Bash");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "exit 42");
    char *output = NULL;
    int rc = bash->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 42);
    ASSERT(strstr(output, "[exit: 42]") != NULL);
    free(output);
    return 0;
}

static int test_bash_timeout(void) {
    const tool_def_t *bash = tools_find("Bash");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "sleep 10");
    cJSON_AddNumberToObject(input, "timeout", 1);
    char *output = NULL;
    int rc = bash->execute(input, &output, NULL);
    cJSON_Delete(input);
    (void)rc;
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "timed out") != NULL);
    free(output);
    return 0;
}

static int test_bash_working_dir(void) {
    const tool_def_t *bash = tools_find("Bash");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "pwd");
    cJSON_AddStringToObject(input, "working_directory", "/tmp");
    char *output = NULL;
    int rc = bash->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "/tmp") != NULL || strstr(output, "/private/tmp") != NULL);
    free(output);
    return 0;
}

static int test_file_read(void) {
    /* Write a temp file first */
    const char *path = "/tmp/nevercode_test_read.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "line1\nline2\nline3\n");
    fclose(f);

    const tool_def_t *read_tool = tools_find("Read");
    ASSERT_NOT_NULL(read_tool);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    char *output = NULL;
    int rc = read_tool->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "line1") != NULL);
    ASSERT(strstr(output, "line2") != NULL);
    ASSERT(strstr(output, "line3") != NULL);
    free(output);
    unlink(path);
    return 0;
}

static int test_file_read_offset(void) {
    const char *path = "/tmp/nevercode_test_offset.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "a\nb\nc\nd\ne\n");
    fclose(f);

    const tool_def_t *read_tool = tools_find("Read");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    cJSON_AddNumberToObject(input, "offset", 3);
    cJSON_AddNumberToObject(input, "limit", 2);
    char *output = NULL;
    int rc = read_tool->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "c") != NULL);
    ASSERT(strstr(output, "d") != NULL);
    free(output);
    unlink(path);
    return 0;
}

static int test_file_read_not_found(void) {
    const tool_def_t *read_tool = tools_find("Read");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", "/tmp/nevercode_nonexistent_12345.txt");
    char *output = NULL;
    int rc = read_tool->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "error") != NULL);
    free(output);
    return 0;
}

static int test_file_write_and_read(void) {
    const char *path = "/tmp/nevercode_test_write.txt";
    const tool_def_t *write_tool = tools_find("Write");
    const tool_def_t *read_tool = tools_find("Read");

    cJSON *wi = cJSON_CreateObject();
    cJSON_AddStringToObject(wi, "path", path);
    cJSON_AddStringToObject(wi, "content", "written by nevercode test");
    char *wout = NULL;
    int rc = write_tool->execute(wi, &wout, NULL);
    cJSON_Delete(wi);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(wout, "Wrote") != NULL);
    free(wout);

    cJSON *ri = cJSON_CreateObject();
    cJSON_AddStringToObject(ri, "path", path);
    char *rout = NULL;
    rc = read_tool->execute(ri, &rout, NULL);
    cJSON_Delete(ri);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(rout, "written by nevercode test") != NULL);
    free(rout);

    unlink(path);
    return 0;
}

static int test_file_edit(void) {
    const char *path = "/tmp/nevercode_test_edit.txt";
    FILE *f = fopen(path, "w");
    fprintf(f, "hello world\nfoo bar\n");
    fclose(f);

    const tool_def_t *edit = tools_find("Edit");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", path);
    cJSON_AddStringToObject(input, "old_string", "foo bar");
    cJSON_AddStringToObject(input, "new_string", "baz qux");
    char *output = NULL;
    int rc = edit->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "Replaced 1") != NULL);
    free(output);

    f = fopen(path, "r");
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT(strstr(buf, "baz qux") != NULL);
    ASSERT(strstr(buf, "foo bar") == NULL);

    unlink(path);
    return 0;
}

static int test_file_edit_not_found(void) {
    const tool_def_t *edit = tools_find("Edit");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "path", "/tmp/nevercode_test_edit.txt");
    cJSON_AddStringToObject(input, "old_string", "nope");
    cJSON_AddStringToObject(input, "new_string", "yep");
    char *output = NULL;
    int rc = edit->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 1);
    free(output);
    return 0;
}

static int test_glob(void) {
    mkdir("/tmp/nevercode_glob_test", 0755);
    FILE *f1 = fopen("/tmp/nevercode_glob_test/a.c", "w");
    fclose(f1);
    FILE *f2 = fopen("/tmp/nevercode_glob_test/b.c", "w");
    fclose(f2);
    FILE *f3 = fopen("/tmp/nevercode_glob_test/c.txt", "w");
    fclose(f3);

    const tool_def_t *glob = tools_find("Glob");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "pattern", "*.c");
    cJSON_AddStringToObject(input, "path", "/tmp/nevercode_glob_test");
    char *output = NULL;
    int rc = glob->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "a.c") != NULL);
    ASSERT(strstr(output, "b.c") != NULL);
    ASSERT(strstr(output, "c.txt") == NULL);
    free(output);

    unlink("/tmp/nevercode_glob_test/a.c");
    unlink("/tmp/nevercode_glob_test/b.c");
    unlink("/tmp/nevercode_glob_test/c.txt");
    rmdir("/tmp/nevercode_glob_test");
    return 0;
}

static int test_todo(void) {
    const tool_def_t *todo = tools_find("TodoWrite");
    ASSERT_NOT_NULL(todo);

    cJSON *input = cJSON_CreateObject();
    cJSON *todos = cJSON_CreateArray();
    cJSON *t1 = cJSON_CreateObject();
    cJSON_AddStringToObject(t1, "id", "1");
    cJSON_AddStringToObject(t1, "content", "First task");
    cJSON_AddStringToObject(t1, "status", "pending");
    cJSON_AddItemToArray(todos, t1);
    cJSON_AddItemToObject(input, "todos", todos);

    char *output = NULL;
    int rc = todo->execute(input, &output, NULL);
    cJSON_Delete(input);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "First task") != NULL);
    ASSERT(strstr(output, "[ ]") != NULL);
    free(output);
    return 0;
}

int test_tools_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tools");
    RUN_TEST(test_tool_registry);
    RUN_TEST(test_tool_all_registered);
    RUN_TEST(test_bash_echo);
    RUN_TEST(test_bash_exit_code);
    RUN_TEST(test_bash_timeout);
    RUN_TEST(test_bash_working_dir);
    RUN_TEST(test_file_read);
    RUN_TEST(test_file_read_offset);
    RUN_TEST(test_file_read_not_found);
    RUN_TEST(test_file_write_and_read);
    RUN_TEST(test_file_edit);
    RUN_TEST(test_file_edit_not_found);
    RUN_TEST(test_glob);
    RUN_TEST(test_todo);
    TEST_RETURN_FAILED();
}
