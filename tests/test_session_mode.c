#include "test.h"
#include "nevercode.h"
#include "defaults.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

static int test_smode_defaults(void) {
    session_mode_set(0);
    ASSERT_INT_EQ(session_mode_get(), 0);
    ASSERT_STR_EQ(session_mode_name(0), "default");
    return 0;
}

static int test_smode_parse(void) {
    ASSERT_INT_EQ(session_mode_parse("coordinator"), 1);
    ASSERT_INT_EQ(session_mode_parse("plan"), 2);
    ASSERT_INT_EQ(session_mode_parse("default"), 0);
    ASSERT_INT_EQ(session_mode_parse(NULL), 0);
    ASSERT_INT_EQ(session_mode_parse("xyz"), 0);
    return 0;
}

static int test_smode_names(void) {
    ASSERT_STR_EQ(session_mode_name(0), "default");
    ASSERT_STR_EQ(session_mode_name(1), "coordinator");
    ASSERT_STR_EQ(session_mode_name(2), "plan");
    return 0;
}

static int test_smode_set_get(void) {
    session_mode_set(1);
    ASSERT_INT_EQ(session_mode_get(), 1);
    session_mode_set(2);
    ASSERT_INT_EQ(session_mode_get(), 2);
    session_mode_set(0);
    ASSERT_INT_EQ(session_mode_get(), 0);
    return 0;
}

static int test_smode_roundtrip(void) {
    char tmp[256];
    char agents[288];
    char sessions[320];
    snprintf(tmp, sizeof(tmp), "/tmp/nevercode_smode_%d", (int)getpid());
    snprintf(agents, sizeof(agents), "%s/.agents", tmp);
    snprintf(sessions, sizeof(sessions), "%s/.agents/sessions", tmp);

    const char *old_home = getenv(ENV_HOME);
    char old_buf[4096] = {0};
    if (old_home)
        nc_strlcpy(old_buf, old_home, sizeof(old_buf));

    ASSERT_INT_EQ(mkdir(tmp, 0755), 0);
    ASSERT_INT_EQ(mkdir(agents, 0755), 0);
    ASSERT_INT_EQ(setenv("HOME", tmp, 1), 0);
    config_home_reset_cache();

    session_mode_set(1);
    ASSERT_INT_EQ(session_mode_save("test_sess"), 0);

    session_mode_set(0);
    ASSERT_INT_EQ(session_mode_load("test_sess"), 1);
    ASSERT_INT_EQ(session_mode_get(), 1);

    char fpath[384];
    snprintf(fpath, sizeof(fpath), "%s/test_sess.mode", sessions);
    unlink(fpath);
    rmdir(sessions);
    rmdir(agents);
    rmdir(tmp);

    if (old_buf[0])
        setenv("HOME", old_buf, 1);
    else
        unsetenv("HOME");
    config_home_reset_cache();
    session_mode_set(0);
    return 0;
}

static int test_smode_match_same(void) {
    session_mode_set(0);
    ASSERT_TRUE(session_mode_match(NULL));
    return 0;
}

static int test_smode_effort_hint(void) {
    ASSERT_INT_EQ(session_mode_effort_hint(2), EFFORT_MEDIUM);
    ASSERT_INT_EQ(session_mode_effort_hint(3), EFFORT_HIGH);
    ASSERT_INT_EQ(session_mode_effort_hint(1), EFFORT_HIGH);
    ASSERT_INT_EQ(session_mode_effort_hint(4), EFFORT_MEDIUM);
    ASSERT_INT_EQ(session_mode_effort_hint(0), EFFORT_DEFAULT);
    return 0;
}

static int test_smode_readonly(void) {
    ASSERT_TRUE(session_mode_is_readonly(2));
    ASSERT_TRUE(!session_mode_is_readonly(0));
    ASSERT_TRUE(!session_mode_is_readonly(1));
    ASSERT_TRUE(!session_mode_is_readonly(3));
    ASSERT_TRUE(!session_mode_is_readonly(4));
    return 0;
}

static int test_smode_context_hint(void) {
    ASSERT_TRUE(session_mode_context_hint(0) == NULL);
    ASSERT_TRUE(session_mode_context_hint(2) != NULL);
    ASSERT_TRUE(session_mode_context_hint(3) != NULL);
    ASSERT_TRUE(session_mode_context_hint(1) != NULL);
    ASSERT_TRUE(session_mode_context_hint(4) != NULL);
    ASSERT_TRUE(strstr(session_mode_context_hint(2), "plan") != NULL);
    ASSERT_TRUE(strstr(session_mode_context_hint(3), "execute") != NULL);
    return 0;
}

static int test_smode_delegation_depth(void) {
    ASSERT_TRUE(NC_MAX_DELEGATION_DEPTH >= 1);
    ASSERT_TRUE(NC_MAX_DELEGATION_DEPTH <= 10);
    ASSERT_TRUE(NC_AGENT_DEFAULT_TURNS > 0);
    return 0;
}

int test_session_mode_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_mode");

    RUN_TEST(test_smode_defaults);
    RUN_TEST(test_smode_parse);
    RUN_TEST(test_smode_names);
    RUN_TEST(test_smode_set_get);
    RUN_TEST(test_smode_roundtrip);
    RUN_TEST(test_smode_match_same);
    RUN_TEST(test_smode_effort_hint);
    RUN_TEST(test_smode_readonly);
    RUN_TEST(test_smode_context_hint);
    RUN_TEST(test_smode_delegation_depth);

    TEST_RETURN_FAILED();
}
