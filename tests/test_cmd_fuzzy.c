/*
 * test_cmd_fuzzy.c — Tests for slash command fuzzy matching.
 */
#include "test.h"
#include "nevercode.h"

static const char *TEST_CMDS[] = {"/help",    "/commit", "/compact", "/context", "/config",
                                  "/cost",    "/diff",   "/doctor",  "/effort",  "/exit",
                                  "/history", "/memory", "/plan",    "/review",  "/status",
                                  "/version", "/health", "/pr",      "/skills",  "/clear"};

static const int N_CMDS = NC_ARRAY_LEN(TEST_CMDS);

static int t_exact_match(void) {
    cmd_fuzzy_result_t results[5];
    int n = cmd_fuzzy_match("/help", TEST_CMDS, N_CMDS, results, 5);
    ASSERT(n >= 1);
    ASSERT_STR_EQ(results[0].name, "/help");
    return 0;
}

static int t_prefix_match(void) {
    cmd_fuzzy_result_t results[5];
    int n = cmd_fuzzy_match("/com", TEST_CMDS, N_CMDS, results, 5);
    ASSERT(n >= 2);
    return 0;
}

static int t_typo_match(void) {
    cmd_fuzzy_result_t results[5];
    int n = cmd_fuzzy_match("/comit", TEST_CMDS, N_CMDS, results, 5);
    ASSERT(n >= 1);
    ASSERT_STR_EQ(results[0].name, "/commit");
    return 0;
}

static int t_no_match(void) {
    cmd_fuzzy_result_t results[5];
    int n = cmd_fuzzy_match("/xyzabc", TEST_CMDS, N_CMDS, results, 5);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

static int t_null_safety(void) {
    cmd_fuzzy_result_t results[5];
    ASSERT_INT_EQ(cmd_fuzzy_match(NULL, TEST_CMDS, N_CMDS, results, 5), 0);
    ASSERT_INT_EQ(cmd_fuzzy_match("/help", NULL, 0, results, 5), 0);
    ASSERT_INT_EQ(cmd_fuzzy_match("/help", TEST_CMDS, N_CMDS, NULL, 5), 0);
    return 0;
}

static int t_macros(void) {
    ASSERT(CMD_FUZZY_MAX_CANDIDATES > 0);
    ASSERT(CMD_FUZZY_THRESHOLD > 0);
    ASSERT(CMD_FUZZY_PREFIX_BONUS > 0);
    return 0;
}

int test_cmd_fuzzy_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("cmd_fuzzy");
    RUN_TEST(t_exact_match);
    RUN_TEST(t_prefix_match);
    RUN_TEST(t_typo_match);
    RUN_TEST(t_no_match);
    RUN_TEST(t_null_safety);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
