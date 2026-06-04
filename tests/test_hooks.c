#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char tmp_path[256];

static void write_hooks_file(const char *json) {
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/test_hooks_%d.json", getpid());
    FILE *f = fopen(tmp_path, "w");
    fputs(json, f);
    fclose(f);
}

static void cleanup(void) {
    unlink(tmp_path);
}

static int test_hooks_init(void) {
    hook_ctx_t h;
    hooks_init(&h);
    ASSERT_INT_EQ(h.num_hooks, 0);
    hooks_free(&h);
    return 0;
}

static int test_hooks_load_empty(void) {
    write_hooks_file("{\"hooks\":[]}");
    hook_ctx_t h;
    hooks_init(&h);
    int n = hooks_load(&h, tmp_path);
    ASSERT_INT_EQ(n, 0);
    hooks_free(&h);
    cleanup();
    return 0;
}

static int test_hooks_load_one(void) {
    write_hooks_file("{\"hooks\":[{\"event\":\"PreToolUse\","
                     "\"command\":\"echo ok\",\"timeout\":5}]}");
    hook_ctx_t h;
    hooks_init(&h);
    int n = hooks_load(&h, tmp_path);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(h.hooks[0].event, HOOK_PRE_TOOL_USE);
    ASSERT_INT_EQ(h.hooks[0].timeout, 5);
    hooks_free(&h);
    cleanup();
    return 0;
}

static int test_hooks_load_tool_pattern(void) {
    write_hooks_file("{\"hooks\":[{\"event\":\"PreToolUse\","
                     "\"command\":\"true\",\"toolPattern\":\"Bash\"}]}");
    hook_ctx_t h;
    hooks_init(&h);
    hooks_load(&h, tmp_path);
    ASSERT_INT_EQ(h.num_hooks, 1);
    ASSERT_STR_EQ(h.hooks[0].tool_pattern, "Bash");
    hooks_free(&h);
    cleanup();
    return 0;
}

static int test_hooks_run_allow(void) {
    write_hooks_file("{\"hooks\":[{\"event\":\"PreToolUse\","
                     "\"command\":\"exit 0\"}]}");
    hook_ctx_t h;
    hooks_init(&h);
    hooks_load(&h, tmp_path);
    hook_decision_t d = hooks_run(&h, HOOK_PRE_TOOL_USE, "Bash", "{}", NULL);
    ASSERT_INT_EQ(d, HOOK_DECISION_PROCEED);
    hooks_free(&h);
    cleanup();
    return 0;
}

static int test_hooks_run_block(void) {
    write_hooks_file("{\"hooks\":[{\"event\":\"PreToolUse\","
                     "\"command\":\"exit 2\"}]}");
    hook_ctx_t h;
    hooks_init(&h);
    hooks_load(&h, tmp_path);
    hook_decision_t d = hooks_run(&h, HOOK_PRE_TOOL_USE, "Bash", "{}", NULL);
    ASSERT_INT_EQ(d, HOOK_DECISION_BLOCK);
    hooks_free(&h);
    cleanup();
    return 0;
}

static int test_hooks_run_skip(void) {
    write_hooks_file("{\"hooks\":[{\"event\":\"PreToolUse\","
                     "\"command\":\"exit 3\"}]}");
    hook_ctx_t h;
    hooks_init(&h);
    hooks_load(&h, tmp_path);
    hook_decision_t d = hooks_run(&h, HOOK_PRE_TOOL_USE, "Bash", "{}", NULL);
    ASSERT_INT_EQ(d, HOOK_DECISION_SKIP);
    hooks_free(&h);
    cleanup();
    return 0;
}

static int test_hooks_tool_pattern_match(void) {
    write_hooks_file("{\"hooks\":[{\"event\":\"PreToolUse\","
                     "\"command\":\"exit 2\",\"toolPattern\":\"Bash\"}]}");
    hook_ctx_t h;
    hooks_init(&h);
    hooks_load(&h, tmp_path);
    /* Should block Bash */
    hook_decision_t d1 = hooks_run(&h, HOOK_PRE_TOOL_USE, "Bash", "{}", NULL);
    ASSERT_INT_EQ(d1, HOOK_DECISION_BLOCK);
    /* Should not block Read */
    hook_decision_t d2 = hooks_run(&h, HOOK_PRE_TOOL_USE, "Read", "{}", NULL);
    ASSERT_INT_EQ(d2, HOOK_DECISION_PROCEED);
    hooks_free(&h);
    cleanup();
    return 0;
}

static int test_hooks_env_vars(void) {
    write_hooks_file("{\"hooks\":[{\"event\":\"PreToolUse\","
                     "\"command\":\"echo $NEVERCODE_TOOL_NAME\"}]}");
    hook_ctx_t h;
    hooks_init(&h);
    hooks_load(&h, tmp_path);
    char *ctx = NULL;
    hook_decision_t d = hooks_run(&h, HOOK_PRE_TOOL_USE, "Bash", "{}", &ctx);
    ASSERT_INT_EQ(d, HOOK_DECISION_PROCEED);
    ASSERT_NOT_NULL(ctx);
    ASSERT(strstr(ctx, "Bash") != NULL);
    free(ctx);
    hooks_free(&h);
    cleanup();
    return 0;
}

static int test_hooks_event_mismatch(void) {
    write_hooks_file("{\"hooks\":[{\"event\":\"PostToolUse\","
                     "\"command\":\"exit 2\"}]}");
    hook_ctx_t h;
    hooks_init(&h);
    hooks_load(&h, tmp_path);
    hook_decision_t d = hooks_run(&h, HOOK_PRE_TOOL_USE, "Bash", "{}", NULL);
    ASSERT_INT_EQ(d, HOOK_DECISION_PROCEED);
    hooks_free(&h);
    cleanup();
    return 0;
}

static int test_hooks_nonexistent_file(void) {
    hook_ctx_t h;
    hooks_init(&h);
    int n = hooks_load(&h, "/tmp/nonexistent_hooks_12345.json");
    ASSERT_INT_EQ(n, -1);
    ASSERT_INT_EQ(h.num_hooks, 0);
    hooks_free(&h);
    return 0;
}

int test_hooks_all(void) {
    TEST_SUITE("hooks");
    TEST_COUNTERS;
    RUN_TEST(test_hooks_init);
    RUN_TEST(test_hooks_load_empty);
    RUN_TEST(test_hooks_load_one);
    RUN_TEST(test_hooks_load_tool_pattern);
    RUN_TEST(test_hooks_run_allow);
    RUN_TEST(test_hooks_run_block);
    RUN_TEST(test_hooks_run_skip);
    RUN_TEST(test_hooks_tool_pattern_match);
    RUN_TEST(test_hooks_env_vars);
    RUN_TEST(test_hooks_event_mismatch);
    RUN_TEST(test_hooks_nonexistent_file);
    TEST_RETURN_FAILED();
}
