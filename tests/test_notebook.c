#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *SAMPLE_NOTEBOOK =
    "{"
    "  \"nbformat\": 4,"
    "  \"nbformat_minor\": 5,"
    "  \"metadata\": {"
    "    \"language_info\": { \"name\": \"python\" }"
    "  },"
    "  \"cells\": ["
    "    {"
    "      \"cell_type\": \"code\","
    "      \"id\": \"abc123\","
    "      \"source\": [\"print('hello')\\n\"],"
    "      \"metadata\": {},"
    "      \"execution_count\": 1,"
    "      \"outputs\": [{\"output_type\": \"stream\", \"text\": [\"hello\\n\"]}]"
    "    },"
    "    {"
    "      \"cell_type\": \"markdown\","
    "      \"id\": \"def456\","
    "      \"source\": [\"# Title\\n\"],"
    "      \"metadata\": {}"
    "    }"
    "  ]"
    "}";

static char tmp_path[256];

static void setup_notebook(void) {
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/test_nb_%d.ipynb", getpid());
    FILE *f = fopen(tmp_path, "w");
    fputs(SAMPLE_NOTEBOOK, f);
    fclose(f);
}

static void cleanup_notebook(void) {
    unlink(tmp_path);
}

static cJSON *read_notebook_json(void) {
    char *buf = strbuf_read_file(tmp_path, NULL);
    if (!buf)
        return NULL;
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    return j;
}

extern void tool_notebook_edit_register(void);

static int test_nb_replace_cell(void) {
    setup_notebook();
    tools_register_all();

    const tool_def_t *t = tools_find("NotebookEdit");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "notebook_path", tmp_path);
    cJSON_AddNumberToObject(input, "cell_index", 0);
    cJSON_AddStringToObject(input, "new_source", "x = 42\nprint(x)\n");
    cJSON_AddStringToObject(input, "edit_mode", "replace");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "replaced") != NULL);

    cJSON *nb = read_notebook_json();
    ASSERT_NOT_NULL(nb);
    cJSON *cells = cJSON_GetObjectItem(nb, "cells");
    ASSERT_INT_EQ(cJSON_GetArraySize(cells), 2);
    cJSON *c0 = cJSON_GetArrayItem(cells, 0);
    cJSON *src = cJSON_GetObjectItem(c0, "source");
    ASSERT(cJSON_GetArraySize(src) >= 1);
    cJSON *exec_count = cJSON_GetObjectItem(c0, "execution_count");
    ASSERT(cJSON_IsNull(exec_count));
    cJSON *outputs = cJSON_GetObjectItem(c0, "outputs");
    ASSERT_INT_EQ(cJSON_GetArraySize(outputs), 0);

    cJSON_Delete(nb);
    cJSON_Delete(input);
    free(output);
    cleanup_notebook();
    return 0;
}

static int test_nb_insert_cell(void) {
    setup_notebook();

    const tool_def_t *t = tools_find("NotebookEdit");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "notebook_path", tmp_path);
    cJSON_AddNumberToObject(input, "cell_index", 1);
    cJSON_AddStringToObject(input, "new_source", "# New markdown cell\n");
    cJSON_AddStringToObject(input, "cell_type", "markdown");
    cJSON_AddStringToObject(input, "edit_mode", "insert");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "inserted") != NULL);

    cJSON *nb = read_notebook_json();
    cJSON *cells = cJSON_GetObjectItem(nb, "cells");
    ASSERT_INT_EQ(cJSON_GetArraySize(cells), 3);

    cJSON_Delete(nb);
    cJSON_Delete(input);
    free(output);
    cleanup_notebook();
    return 0;
}

static int test_nb_delete_cell(void) {
    setup_notebook();

    const tool_def_t *t = tools_find("NotebookEdit");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "notebook_path", tmp_path);
    cJSON_AddNumberToObject(input, "cell_index", 0);
    cJSON_AddStringToObject(input, "new_source", "");
    cJSON_AddStringToObject(input, "edit_mode", "delete");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "deleted") != NULL);

    cJSON *nb = read_notebook_json();
    cJSON *cells = cJSON_GetObjectItem(nb, "cells");
    ASSERT_INT_EQ(cJSON_GetArraySize(cells), 1);
    const char *ct =
        cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(cells, 0), "cell_type"));
    ASSERT_STR_EQ(ct, "markdown");

    cJSON_Delete(nb);
    cJSON_Delete(input);
    free(output);
    cleanup_notebook();
    return 0;
}

static int test_nb_invalid_extension(void) {
    const tool_def_t *t = tools_find("NotebookEdit");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "notebook_path", "/tmp/test.py");
    cJSON_AddNumberToObject(input, "cell_index", 0);
    cJSON_AddStringToObject(input, "new_source", "x=1");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, ".ipynb") != NULL);

    cJSON_Delete(input);
    free(output);
    return 0;
}

static int test_nb_out_of_range(void) {
    setup_notebook();

    const tool_def_t *t = tools_find("NotebookEdit");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "notebook_path", tmp_path);
    cJSON_AddNumberToObject(input, "cell_index", 99);
    cJSON_AddStringToObject(input, "new_source", "x=1");
    cJSON_AddStringToObject(input, "edit_mode", "replace");

    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "out of range") != NULL);

    cJSON_Delete(input);
    free(output);
    cleanup_notebook();
    return 0;
}

static int test_nb_tool_flags(void) {
    const tool_def_t *t = tools_find("NotebookEdit");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == false);
    ASSERT(t->concurrency_safe == false);
    return 0;
}

int test_notebook_all(void) {
    TEST_SUITE("notebook_edit");
    TEST_COUNTERS;
    RUN_TEST(test_nb_replace_cell);
    RUN_TEST(test_nb_insert_cell);
    RUN_TEST(test_nb_delete_cell);
    RUN_TEST(test_nb_invalid_extension);
    RUN_TEST(test_nb_out_of_range);
    RUN_TEST(test_nb_tool_flags);
    TEST_RETURN_FAILED();
}
