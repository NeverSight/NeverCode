#include "test.h"
#include "nevercode.h"
#include <string.h>

static int test_parse_basic_agent(void) {
    const char *md = "---\n"
                     "name: security-auditor\n"
                     "description: Analyze code for security vulnerabilities\n"
                     "model: haiku\n"
                     "maxTurns: 15\n"
                     "background: true\n"
                     "color: orange\n"
                     "---\n\n"
                     "You are a security auditor.\n";

    agent_def_t a;
    agent_def_init(&a);
    ASSERT(agent_def_parse_md(&a, md) == 0);
    ASSERT_STR_EQ(a.name, "security-auditor");
    ASSERT_STR_EQ(a.description, "Analyze code for security vulnerabilities");
    ASSERT_STR_EQ(a.model, "haiku");
    ASSERT_INT_EQ(a.max_turns, 15);
    ASSERT(a.background == true);
    ASSERT(a.color == 'o');
    ASSERT_NOT_NULL(a.system_prompt);
    ASSERT(strstr(a.system_prompt, "security auditor") != NULL);
    agent_def_free(&a);
    return 0;
}

static int test_parse_tool_lists(void) {
    const char *md = "---\n"
                     "name: read-only-agent\n"
                     "description: Read-only agent\n"
                     "tools:\n"
                     "  - Bash\n"
                     "  - Read\n"
                     "  - Grep\n"
                     "disallowedTools:\n"
                     "  - Write\n"
                     "  - Edit\n"
                     "---\n\n"
                     "Read only.\n";

    agent_def_t a;
    agent_def_init(&a);
    ASSERT(agent_def_parse_md(&a, md) == 0);
    ASSERT_INT_EQ(a.num_allowed_tools, 3);
    ASSERT_STR_EQ(a.allowed_tools[0], "Bash");
    ASSERT_STR_EQ(a.allowed_tools[1], "Read");
    ASSERT_STR_EQ(a.allowed_tools[2], "Grep");
    ASSERT_INT_EQ(a.num_disallowed_tools, 2);
    ASSERT_STR_EQ(a.disallowed_tools[0], "Write");
    ASSERT_STR_EQ(a.disallowed_tools[1], "Edit");
    agent_def_free(&a);
    return 0;
}

static int test_tool_allowed_with_allowlist(void) {
    agent_def_t a;
    agent_def_init(&a);
    a.allowed_tools[0] = nc_xstrdup("Read");
    a.allowed_tools[1] = nc_xstrdup("Grep");
    a.num_allowed_tools = 2;

    ASSERT(agent_def_tool_allowed(&a, "Read") == true);
    ASSERT(agent_def_tool_allowed(&a, "Grep") == true);
    ASSERT(agent_def_tool_allowed(&a, "Write") == false);
    ASSERT(agent_def_tool_allowed(&a, "Bash") == false);
    agent_def_free(&a);
    return 0;
}

static int test_tool_allowed_with_denylist(void) {
    agent_def_t a;
    agent_def_init(&a);
    a.disallowed_tools[0] = nc_xstrdup("Write");
    a.disallowed_tools[1] = nc_xstrdup("Edit");
    a.num_disallowed_tools = 2;

    ASSERT(agent_def_tool_allowed(&a, "Read") == true);
    ASSERT(agent_def_tool_allowed(&a, "Write") == false);
    ASSERT(agent_def_tool_allowed(&a, "Edit") == false);
    ASSERT(agent_def_tool_allowed(&a, "Bash") == true);
    agent_def_free(&a);
    return 0;
}

static int test_tool_allowed_wildcard(void) {
    agent_def_t a;
    agent_def_init(&a);
    a.allowed_tools[0] = nc_xstrdup("*");
    a.num_allowed_tools = 1;
    a.disallowed_tools[0] = nc_xstrdup("Agent");
    a.num_disallowed_tools = 1;

    ASSERT(agent_def_tool_allowed(&a, "Read") == true);
    ASSERT(agent_def_tool_allowed(&a, "Bash") == true);
    ASSERT(agent_def_tool_allowed(&a, "Agent") == false);
    agent_def_free(&a);
    return 0;
}

static int test_builtin_agents(void) {
    const agent_def_t *e = agent_def_find("explore");
    ASSERT_NOT_NULL(e);
    ASSERT(e->read_only == true);
    ASSERT_INT_EQ(e->max_turns, 15);
    ASSERT_NOT_NULL(e->system_prompt);

    const agent_def_t *p = agent_def_find("plan");
    ASSERT_NOT_NULL(p);
    ASSERT(p->read_only == true);

    const agent_def_t *g = agent_def_find("general");
    ASSERT_NOT_NULL(g);
    ASSERT(g->read_only == false);

    const agent_def_t *v = agent_def_find("verification");
    ASSERT_NOT_NULL(v);
    ASSERT(v->background == true);
    ASSERT(v->color == 'r');
    return 0;
}

static int test_builtin_list(void) {
    const agent_def_t *list[8];
    int n = agent_def_list_builtin(list, 8);
    ASSERT_INT_EQ(n, 4);
    return 0;
}

static int test_parse_effort(void) {
    const char *md = "---\n"
                     "name: deep-thinker\n"
                     "description: Deep reasoning agent\n"
                     "effort: high\n"
                     "read_only: true\n"
                     "---\n\n"
                     "Think deeply.\n";

    agent_def_t a;
    agent_def_init(&a);
    ASSERT(agent_def_parse_md(&a, md) == 0);
    ASSERT_INT_EQ(a.effort, 3);
    ASSERT(a.read_only == true);
    agent_def_free(&a);
    return 0;
}

static int test_parse_invalid(void) {
    agent_def_t a;
    agent_def_init(&a);
    ASSERT(agent_def_parse_md(&a, "no frontmatter") != 0);
    ASSERT(agent_def_parse_md(&a, NULL) != 0);
    ASSERT(agent_def_parse_md(&a, "---\n") != 0);
    agent_def_free(&a);
    return 0;
}

static int test_find_unknown(void) {
    ASSERT_NULL(agent_def_find("nonexistent"));
    return 0;
}

int test_custom_agents_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("custom_agents");
    RUN_TEST(test_parse_basic_agent);
    RUN_TEST(test_parse_tool_lists);
    RUN_TEST(test_tool_allowed_with_allowlist);
    RUN_TEST(test_tool_allowed_with_denylist);
    RUN_TEST(test_tool_allowed_wildcard);
    RUN_TEST(test_builtin_agents);
    RUN_TEST(test_builtin_list);
    RUN_TEST(test_parse_effort);
    RUN_TEST(test_parse_invalid);
    RUN_TEST(test_find_unknown);
    TEST_RETURN_FAILED();
}
