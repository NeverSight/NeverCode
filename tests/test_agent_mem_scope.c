#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int t_dir_user(void) {
    char dir[NC_PATH_MAX];
    ASSERT_INT_EQ(agent_memory_dir(dir, sizeof(dir), "explorer", AGENT_MEM_USER), 0);
    ASSERT_STR_CONTAINS(dir, "agent-memory");
    ASSERT_STR_CONTAINS(dir, "explorer");
    return 0;
}

static int t_dir_project(void) {
    char dir[NC_PATH_MAX];
    ASSERT_INT_EQ(agent_memory_dir(dir, sizeof(dir), "reviewer", AGENT_MEM_PROJECT), 0);
    ASSERT_STR_CONTAINS(dir, "agent-memory");
    ASSERT_STR_CONTAINS(dir, "reviewer");
    return 0;
}

static int t_dir_local(void) {
    char dir[NC_PATH_MAX];
    ASSERT_INT_EQ(agent_memory_dir(dir, sizeof(dir), "verifier", AGENT_MEM_LOCAL), 0);
    ASSERT_STR_CONTAINS(dir, "agent-memory-local");
    ASSERT_STR_CONTAINS(dir, "verifier");
    return 0;
}

static int t_sanitize(void) {
    char dir[NC_PATH_MAX];
    ASSERT_INT_EQ(agent_memory_dir(dir, sizeof(dir), "plugin:my-agent", AGENT_MEM_USER), 0);
    ASSERT_STR_CONTAINS(dir, "plugin-my-agent");
    return 0;
}

static int t_null_input(void) {
    char dir[NC_PATH_MAX];
    ASSERT(agent_memory_dir(dir, sizeof(dir), NULL, AGENT_MEM_USER) != 0);
    ASSERT(agent_memory_dir(dir, sizeof(dir), "", AGENT_MEM_USER) != 0);
    return 0;
}

static int t_prompt_no_dir(void) {
    char *p = agent_memory_prompt("nonexistent-agent-type-xyz", AGENT_MEM_LOCAL, 1000);
    ASSERT(p == NULL);
    return 0;
}

int test_agent_mem_scope_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_mem_scope");
    RUN_TEST(t_dir_user);
    RUN_TEST(t_dir_project);
    RUN_TEST(t_dir_local);
    RUN_TEST(t_sanitize);
    RUN_TEST(t_null_input);
    RUN_TEST(t_prompt_no_dir);
    TEST_RETURN_FAILED();
}
