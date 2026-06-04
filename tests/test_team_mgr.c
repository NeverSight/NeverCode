#include "test.h"
#include "nevercode.h"
static int t1(void) {
    team_mgr_init();
    ASSERT_INT_EQ(0, team_count());
    team_mgr_free();
    return 0;
}
static int t2(void) {
    team_mgr_init();
    int id = team_create("alpha", "solve ctf");
    ASSERT_GE(id, 0);
    ASSERT_INT_EQ(1, team_count());
    ASSERT_STR_EQ("alpha", team_name(id));
    ASSERT_STR_EQ("solve ctf", team_goal(id));
    team_mgr_free();
    return 0;
}
static int t3(void) {
    team_mgr_init();
    int id = team_create("t", "g");
    team_add_member(id, "agent1", "leader");
    team_add_member(id, "agent2", "worker");
    ASSERT_INT_EQ(2, team_member_count(id));
    team_remove_member(id, "agent1");
    ASSERT_INT_EQ(1, team_member_count(id));
    team_disband(id);
    char *s = team_format_status(id);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "disbanded");
    free(s);
    team_mgr_free();
    return 0;
}
static int t4(void) {
    team_mgr_init();
    team_create("a", "g1");
    team_create("b", "g2");
    char *s = team_format_all();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Teams: 2");
    free(s);
    team_mgr_free();
    return 0;
}
int test_team_mgr_all(void) {
    TEST_SUITE("team_mgr");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    TEST_RETURN_FAILED();
}
