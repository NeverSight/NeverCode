#include "test.h"
#include "nevercode.h"
#include "config.h"
#include "tools.h"

static int test_agent_schema_has_type(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("Agent");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->input_schema);
    cJSON *props = cJSON_GetObjectItem(t->input_schema, "properties");
    ASSERT_NOT_NULL(props);
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "agent_type"));
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "max_turns"));
    ASSERT_NOT_NULL(cJSON_GetObjectItem(props, "prompt"));
    return 0;
}

static int test_config_effort_init(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT_INT_EQ(cfg.effort, 0);
    ASSERT_INT_EQ(cfg.max_turns, 0);
    config_free(&cfg);
    return 0;
}

static int test_config_effort_values(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.effort = 1;
    ASSERT_INT_EQ(cfg.effort, 1);
    cfg.effort = 2;
    ASSERT_INT_EQ(cfg.effort, 2);
    cfg.effort = 3;
    ASSERT_INT_EQ(cfg.effort, 3);
    config_free(&cfg);
    return 0;
}

static int test_config_max_turns(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.max_turns = 25;
    ASSERT_INT_EQ(cfg.max_turns, 25);
    config_free(&cfg);
    return 0;
}

static int test_explore_agent_readonly(void) {
    const agent_def_t *e = agent_def_find("explore");
    ASSERT_NOT_NULL(e);
    ASSERT(agent_def_tool_allowed(e, "Read") == true);
    ASSERT(agent_def_tool_allowed(e, "Grep") == true);
    ASSERT(agent_def_tool_allowed(e, "Glob") == true);
    ASSERT(agent_def_tool_allowed(e, "Write") == false);
    ASSERT(agent_def_tool_allowed(e, "Edit") == false);
    ASSERT(agent_def_tool_allowed(e, "Agent") == false);
    return 0;
}

static int test_general_agent_full_access(void) {
    const agent_def_t *g = agent_def_find("general");
    ASSERT_NOT_NULL(g);
    ASSERT(agent_def_tool_allowed(g, "Read") == true);
    ASSERT(agent_def_tool_allowed(g, "Write") == true);
    ASSERT(agent_def_tool_allowed(g, "Bash") == true);
    ASSERT(agent_def_tool_allowed(g, "Agent") == false);
    return 0;
}

static int test_verification_agent_props(void) {
    const agent_def_t *v = agent_def_find("verification");
    ASSERT_NOT_NULL(v);
    ASSERT(v->background == true);
    ASSERT(v->color == 'r');
    ASSERT_INT_EQ(v->max_turns, 25);
    ASSERT(agent_def_tool_allowed(v, "Write") == false);
    ASSERT(agent_def_tool_allowed(v, "Edit") == false);
    return 0;
}

int test_agent_enhanced_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_enhanced");
    RUN_TEST(test_agent_schema_has_type);
    RUN_TEST(test_config_effort_init);
    RUN_TEST(test_config_effort_values);
    RUN_TEST(test_config_max_turns);
    RUN_TEST(test_explore_agent_readonly);
    RUN_TEST(test_general_agent_full_access);
    RUN_TEST(test_verification_agent_props);
    TEST_RETURN_FAILED();
}
