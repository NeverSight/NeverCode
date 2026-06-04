#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

extern void team_memory_sync_init(void);
extern void team_memory_sync_free(void);
extern void team_memory_sync_set_enabled(bool on);
extern bool team_memory_sync_enabled(void);
extern int team_memory_add_secret_rule(const char *, const char *);
extern bool team_memory_scan_secrets(const char *text);
extern int team_memory_sync_put(const char *, const char *, const char *);
extern const char *team_memory_sync_get(const char *);
extern int team_memory_sync_mark_synced(const char *);
extern int team_memory_sync_unsynced_count(void);
extern int team_memory_sync_count(void);
extern int team_memory_sync_blocked_count(void);
extern int team_memory_sync_rule_count(void);
extern char *team_memory_sync_format(void);

static int test_init(void) {
    team_memory_sync_init();
    ASSERT_TRUE(team_memory_sync_enabled());
    ASSERT_INT_EQ(0, team_memory_sync_count());
    team_memory_sync_free();
    return 0;
}

static int test_put_get(void) {
    team_memory_sync_init();
    int rc = team_memory_sync_put("goal", "build CLI", "agent-1");
    ASSERT_INT_EQ(0, rc);
    ASSERT_STR_EQ("build CLI", team_memory_sync_get("goal"));
    ASSERT_INT_EQ(1, team_memory_sync_count());
    team_memory_sync_free();
    return 0;
}

static int test_secret_blocking(void) {
    team_memory_sync_init();
    int rc = team_memory_sync_put("key", "my AKIA1234567890ABCDEF is secret", "a1");
    ASSERT_INT_EQ(-2, rc);
    ASSERT_INT_EQ(0, team_memory_sync_count());
    ASSERT_INT_EQ(1, team_memory_sync_blocked_count());
    team_memory_sync_free();
    return 0;
}

static int test_scan_secrets(void) {
    team_memory_sync_init();
    ASSERT_TRUE(team_memory_scan_secrets("token: ghp_abc123"));
    ASSERT_TRUE(team_memory_scan_secrets("key AKIA12345"));
    ASSERT_FALSE(team_memory_scan_secrets("normal text"));
    team_memory_sync_free();
    return 0;
}

static int test_sync_mark(void) {
    team_memory_sync_init();
    team_memory_sync_put("k1", "v1", "a1");
    team_memory_sync_put("k2", "v2", "a2");
    ASSERT_INT_EQ(2, team_memory_sync_unsynced_count());

    team_memory_sync_mark_synced("k1");
    ASSERT_INT_EQ(1, team_memory_sync_unsynced_count());

    team_memory_sync_free();
    return 0;
}

static int test_format(void) {
    team_memory_sync_init();
    team_memory_sync_put("x", "y", "agent-0");
    char *fmt = team_memory_sync_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "Team Memory");
    ASSERT_STR_CONTAINS(fmt, "agent-0");
    free(fmt);
    team_memory_sync_free();
    return 0;
}

static int test_default_rules(void) {
    team_memory_sync_init();
    int n = team_memory_sync_rule_count();
    ASSERT_GT(n, 5);
    team_memory_sync_free();
    return 0;
}

int test_team_memory_sync_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("team_memory_sync");
    RUN_TEST(test_init);
    RUN_TEST(test_put_get);
    RUN_TEST(test_secret_blocking);
    RUN_TEST(test_scan_secrets);
    RUN_TEST(test_sync_mark);
    RUN_TEST(test_format);
    RUN_TEST(test_default_rules);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("team_memory_sync");
    RUN_TEST(test_init);
    RUN_TEST(test_put_get);
    RUN_TEST(test_secret_blocking);
    RUN_TEST(test_scan_secrets);
    RUN_TEST(test_sync_mark);
    RUN_TEST(test_format);
    RUN_TEST(test_default_rules);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
