/*
 * test_todo_deps.c — Tests for todo dependency graph (blocks/blockedBy)
 */

#include "test.h"
#include "tools.h"
#include "nevercode.h"

extern void tool_todo_register(void);

static int run_todo(const char *json, char **out) {
    cJSON *input = cJSON_Parse(json);
    if (!input)
        return -1;
    const tool_def_t *t = tools_find("TodoWrite");
    if (!t) {
        cJSON_Delete(input);
        return -1;
    }
    int rc = t->execute(input, out, NULL);
    cJSON_Delete(input);
    return rc;
}

static int test_basic_deps(void) {
    tool_todo_register();
    char *out = NULL;
    int rc = run_todo("{\"todos\":["
                      "{\"id\":\"a\",\"content\":\"build\",\"status\":\"pending\"},"
                      "{\"id\":\"b\",\"content\":\"test\",\"status\":\"pending\","
                      " \"blockedBy\":[\"a\"]}"
                      "]}",
                      &out);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "[B]");
    ASSERT_STR_CONTAINS(out, "blocked by: a");
    ASSERT_STR_CONTAINS(out, "1 blocked");
    free(out);
    return 0;
}

static int test_deps_resolve_on_complete(void) {
    char *out = NULL;
    int rc = run_todo("{\"todos\":["
                      "{\"id\":\"a\",\"content\":\"build\",\"status\":\"completed\"}"
                      "],\"merge\":true}",
                      &out);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "0 blocked");
    free(out);
    return 0;
}

static int test_bidirectional_wiring(void) {
    char *out = NULL;
    int rc = run_todo("{\"todos\":["
                      "{\"id\":\"x\",\"content\":\"first\",\"status\":\"pending\","
                      " \"blocks\":[\"y\"]},"
                      "{\"id\":\"y\",\"content\":\"second\",\"status\":\"pending\"}"
                      "]}",
                      &out);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "blocked by: x");
    free(out);
    return 0;
}

static int test_owner_display(void) {
    char *out = NULL;
    int rc = run_todo("{\"todos\":["
                      "{\"id\":\"t1\",\"content\":\"review\",\"status\":\"in_progress\","
                      " \"owner\":\"agent-1\"}"
                      "]}",
                      &out);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "@agent-1");
    free(out);
    return 0;
}

static int test_merge_with_deps(void) {
    char *out = NULL;
    run_todo("{\"todos\":["
             "{\"id\":\"m1\",\"content\":\"setup\",\"status\":\"pending\"},"
             "{\"id\":\"m2\",\"content\":\"code\",\"status\":\"pending\","
             " \"blockedBy\":[\"m1\"]}"
             "]}",
             &out);
    free(out);

    out = NULL;
    int rc = run_todo("{\"todos\":["
                      "{\"id\":\"m1\",\"content\":\"setup\",\"status\":\"completed\"}"
                      "],\"merge\":true}",
                      &out);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "0 blocked");
    free(out);
    return 0;
}

static int test_summary_counts(void) {
    char *out = NULL;
    int rc = run_todo("{\"todos\":["
                      "{\"id\":\"s1\",\"content\":\"done\",\"status\":\"completed\"},"
                      "{\"id\":\"s2\",\"content\":\"wip\",\"status\":\"in_progress\"},"
                      "{\"id\":\"s3\",\"content\":\"wait\",\"status\":\"pending\"},"
                      "{\"id\":\"s4\",\"content\":\"blocked\",\"status\":\"pending\","
                      " \"blockedBy\":[\"s2\"]}"
                      "]}",
                      &out);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "1 done");
    ASSERT_STR_CONTAINS(out, "1 active");
    ASSERT_STR_CONTAINS(out, "1 pending");
    ASSERT_STR_CONTAINS(out, "1 blocked");
    free(out);
    return 0;
}

static int test_invalid_input(void) {
    char *out = NULL;
    int rc = run_todo("{\"not_todos\":[]}", &out);
    ASSERT(rc != 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_CONTAINS(out, "error");
    free(out);
    return 0;
}

int test_todo_deps_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("todo_deps");
    RUN_TEST(test_basic_deps);
    RUN_TEST(test_deps_resolve_on_complete);
    RUN_TEST(test_bidirectional_wiring);
    RUN_TEST(test_owner_display);
    RUN_TEST(test_merge_with_deps);
    RUN_TEST(test_summary_counts);
    RUN_TEST(test_invalid_input);
    TEST_RETURN_FAILED();
}
