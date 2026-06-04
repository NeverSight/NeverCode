#include "test.h"
extern void team_mgr_init(void);
extern void team_mgr_free(void);
extern int team_create(const char *, const char *);
extern int team_add_member(int, const char *, const char *);
extern int team_disband(int);
extern int team_count(void);
extern int team_member_count(int);
extern const char *team_name(int);
static int t1(void) {
    team_mgr_init();
    int t = team_create("a", "g");
    ASSERT_TRUE(t >= 0);
    ASSERT_INT_EQ(team_count(), 1);
    team_mgr_free();
    return 0;
}
static int t2(void) {
    team_mgr_init();
    int t = team_create("b", "x");
    ASSERT_INT_EQ(team_add_member(t, "a1", "r"), 0);
    ASSERT_INT_EQ(team_member_count(t), 1);
    team_mgr_free();
    return 0;
}
static int t3(void) {
    team_mgr_init();
    int t = team_create("c", "x");
    ASSERT_INT_EQ(team_disband(t), 0);
    team_mgr_free();
    return 0;
}
static int t4(void) {
    team_mgr_init();
    ASSERT_INT_EQ(team_add_member(-1, "x", "y"), -1);
    ASSERT_NULL(team_name(99));
    team_mgr_free();
    return 0;
}
int test_team_tool_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("team_tool");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    TEST_RETURN_FAILED();
}
