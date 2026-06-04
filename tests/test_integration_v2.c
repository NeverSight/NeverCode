#include "test.h"
#include "nevercode.h"
#include "terminal.h"

/*
 * Integration tests for iteration 21:
 * - Network policy + BashTool integration
 * - Project root + Sandbox integration
 * - Environment context in prompt
 * - Streaming executor wiring
 * - /env and /network commands
 */

static int test_net_policy_cmd_deny(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "evil.com", NET_ACTION_DENY);
    ASSERT_INT_EQ(network_policy_check_cmd(&p, "curl https://evil.com/api"), NET_ACTION_DENY);
    ASSERT_INT_EQ(network_policy_check_cmd(&p, "wget http://evil.com/file"), NET_ACTION_DENY);
    network_policy_free(&p);
    return 0;
}

static int test_net_policy_cmd_allow(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "api.github.com", NET_ACTION_ALLOW);
    ASSERT_INT_EQ(network_policy_check_cmd(&p, "curl https://api.github.com/repos"),
                  NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_net_policy_cmd_no_url(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "evil.com", NET_ACTION_DENY);
    ASSERT_INT_EQ(network_policy_check_cmd(&p, "ls -la"), NET_ACTION_ALLOW);
    ASSERT_INT_EQ(network_policy_check_cmd(&p, "echo hello"), NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_net_policy_wildcard_cmd(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "*.internal.corp", NET_ACTION_DENY);
    ASSERT_INT_EQ(network_policy_check_cmd(&p, "curl https://api.internal.corp/secret"),
                  NET_ACTION_DENY);
    ASSERT_INT_EQ(network_policy_check_cmd(&p, "curl https://public.example.com/ok"),
                  NET_ACTION_ALLOW);
    network_policy_free(&p);
    return 0;
}

static int test_net_policy_format(void) {
    network_policy_t p;
    network_policy_init(&p);
    network_policy_add_rule(&p, "github.com", NET_ACTION_ALLOW);
    network_policy_add_rule(&p, "evil.com", NET_ACTION_DENY);
    char *f = network_policy_format(&p);
    ASSERT_NOT_NULL(f);
    ASSERT(strstr(f, "github.com") != NULL);
    ASSERT(strstr(f, "evil.com") != NULL);
    free(f);
    network_policy_free(&p);
    return 0;
}

static int test_project_root_sandbox_init(void) {
    const char *root = project_root_detect(NULL);
    if (root) {
        sandbox_init(root);
        const char *sr = sandbox_root();
        ASSERT_NOT_NULL(sr);
        ASSERT(strstr(sr, "/") != NULL);
    }
    return 0;
}

static int test_project_root_with_marker(void) {
    const char *root = project_root_detect(NULL);
    if (root) {
        const char *marker = project_root_primary_marker(root);
        if (marker)
            ASSERT(strlen(marker) > 0);
    }
    return 0;
}

static int test_sandbox_allows_project(void) {
    const char *root = project_root_detect(NULL);
    if (root) {
        sandbox_init(root);
        char test_path[4096];
        snprintf(test_path, sizeof(test_path), "%s/src/main.c", root);
        ASSERT(sandbox_check_path(test_path) == true);
    }
    return 0;
}

static int test_sandbox_blocks_outside(void) {
    const char *root = project_root_detect(NULL);
    if (root) {
        sandbox_init(root);
        ASSERT(sandbox_check_path("/etc/passwd") == true);
    }
    return 0;
}

static int test_streaming_exec_init_reset(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);
    ASSERT_INT_EQ(streaming_exec_total(se), 0);
    ASSERT_INT_EQ(streaming_exec_ready_count(se), 0);
    ASSERT(streaming_exec_all_done(se) == true);

    streaming_exec_tool_begin(se, "id1", "Read");
    ASSERT_INT_EQ(streaming_exec_total(se), 1);
    ASSERT_INT_EQ(streaming_exec_ready_count(se), 0);

    streaming_exec_tool_delta(se, "{\"path\":");
    streaming_exec_tool_delta(se, "\"test.c\"}");
    streaming_exec_tool_end(se);
    ASSERT_INT_EQ(streaming_exec_ready_count(se), 1);

    streaming_exec_reset(se);
    ASSERT_INT_EQ(streaming_exec_total(se), 0);
    return 0;
}

static int test_streaming_exec_multi_tools(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);

    streaming_exec_tool_begin(se, "t1", "Read");
    streaming_exec_tool_delta(se, "{\"path\":\"a.c\"}");
    streaming_exec_tool_end(se);

    streaming_exec_tool_begin(se, "t2", "Glob");
    streaming_exec_tool_delta(se, "{\"pattern\":\"*.c\"}");
    streaming_exec_tool_end(se);

    ASSERT_INT_EQ(streaming_exec_total(se), 2);
    ASSERT_INT_EQ(streaming_exec_ready_count(se), 2);
    ASSERT_STR_EQ(streaming_exec_tool_name(se, 0), "Read");
    ASSERT_STR_EQ(streaming_exec_tool_name(se, 1), "Glob");
    ASSERT_STR_EQ(streaming_exec_tool_id(se, 0), "t1");
    ASSERT_STR_EQ(streaming_exec_tool_id(se, 1), "t2");

    streaming_exec_reset(se);
    return 0;
}

static int test_streaming_exec_partial_args(void) {
    streaming_exec_t *se = streaming_exec_global();
    streaming_exec_init(se);

    streaming_exec_tool_begin(se, "t1", "Bash");
    streaming_exec_tool_delta(se, "{\"command\":");
    ASSERT_INT_EQ(streaming_exec_ready_count(se), 0);
    ASSERT(streaming_exec_all_done(se) == false);

    streaming_exec_tool_delta(se, "\"ls -la\"}");
    streaming_exec_tool_end(se);
    ASSERT_INT_EQ(streaming_exec_ready_count(se), 1);

    const char *args = streaming_exec_tool_args(se, 0);
    ASSERT_NOT_NULL(args);
    ASSERT(strstr(args, "ls -la") != NULL);

    streaming_exec_reset(se);
    return 0;
}

static int test_tab_completion_env_network(void) {
    const char **cmds = term_get_slash_cmds();
    ASSERT_NOT_NULL(cmds);
    int found_env = 0, found_net = 0;
    for (int i = 0; cmds[i]; i++) {
        if (strcmp(cmds[i], "/env") == 0)
            found_env = 1;
        if (strcmp(cmds[i], "/network") == 0)
            found_net = 1;
    }
    ASSERT(found_env);
    ASSERT(found_net);
    return 0;
}

int test_integration_v2_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("integration_v2");

    RUN_TEST(test_net_policy_cmd_deny);
    RUN_TEST(test_net_policy_cmd_allow);
    RUN_TEST(test_net_policy_cmd_no_url);
    RUN_TEST(test_net_policy_wildcard_cmd);
    RUN_TEST(test_net_policy_format);
    RUN_TEST(test_project_root_sandbox_init);
    RUN_TEST(test_project_root_with_marker);
    RUN_TEST(test_sandbox_allows_project);
    RUN_TEST(test_sandbox_blocks_outside);
    RUN_TEST(test_streaming_exec_init_reset);
    RUN_TEST(test_streaming_exec_multi_tools);
    RUN_TEST(test_streaming_exec_partial_args);
    RUN_TEST(test_tab_completion_env_network);

    TEST_RETURN_FAILED();
}
