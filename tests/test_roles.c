#include "test.h"
#include "nevercode.h"
static int t1(void) {
    agent_roles_init();
    ASSERT(agent_role_count() > 0);
    agent_roles_free();
    return 0;
}
static int t2(void) {
    agent_roles_init();
    ASSERT_NOT_NULL(agent_role_prompt("default"));
    agent_roles_free();
    return 0;
}
static int t3(void) {
    agent_roles_init();
    ASSERT_NOT_NULL(agent_role_prompt("explorer"));
    ASSERT(agent_role_is_readonly("explorer"));
    agent_roles_free();
    return 0;
}
static int t4(void) {
    agent_roles_init();
    ASSERT_NULL(agent_role_prompt("nonexistent-xyz"));
    agent_roles_free();
    return 0;
}
static int t5(void) {
    agent_roles_init();
    for (int i = 0; i < agent_role_count(); i++) {
        ASSERT_NOT_NULL(agent_role_name(i));
        ASSERT_NOT_NULL(agent_role_display(i));
    }
    agent_roles_free();
    return 0;
}
static int t6(void) {
    agent_roles_init();
    ASSERT_NULL(agent_role_name(-1));
    ASSERT_NULL(agent_role_name(999));
    agent_roles_free();
    return 0;
}
static int t7(void) {
    agent_roles_init();
    ASSERT_TRUE(agent_role_tool_allowed("default", "Bash"));
    agent_roles_free();
    return 0;
}
static int t8(void) {
    agent_roles_init();
    char *l = agent_roles_format_list();
    ASSERT_NOT_NULL(l);
    ASSERT_STR_CONTAINS(l, "default");
    ASSERT_STR_CONTAINS(l, "built-in");
    free(l);
    agent_roles_free();
    return 0;
}
static int t9(void) {
    agent_roles_init();
    agent_spawn_opts_t o = {0};
    char *p = NULL, *m = NULL;
    agent_role_apply_to_spawn("explorer", &o, &p, &m);
    ASSERT_NOT_NULL(p);
    free(p);
    free(m);
    agent_roles_free();
    return 0;
}
static int t10(void) {
    agent_roles_init();
    ASSERT_NOT_NULL(agent_role_prompt("recon"));
    ASSERT_FALSE(agent_role_is_readonly("recon"));
    agent_roles_free();
    return 0;
}
int test_roles_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("roles");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(t10);
    TEST_RETURN_FAILED();
}
