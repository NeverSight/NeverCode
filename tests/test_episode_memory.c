#include "test.h"
#include "nevercode.h"
#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int t1(void) {
    episode_memory_init();
    ASSERT_INT_EQ(episode_memory_count(), 0);
    episode_memory_free();
    return 0;
}
static int t2(void) {
    episode_memory_init();
    int i = episode_memory_record("build fail", "hdr", "add incl", 0, 3);
    ASSERT_INT_EQ(i, 0);
    ASSERT_INT_EQ(episode_memory_count(), 1);
    episode_memory_free();
    return 0;
}
static int t3(void) {
    episode_memory_init();
    int i = episode_memory_record("crash", "null", "fix", 0, 2);
    ASSERT_INT_EQ(episode_memory_tag(i, "segfault"), 0);
    episode_memory_free();
    return 0;
}
static int t4(void) {
    episode_memory_init();
    episode_memory_record("linker error undefined symbol", "lib", "add lib", 0, 1);
    episode_memory_record("segfault in parser", "overflow", "bounds", 0, 5);
    int idx[3];
    int n = episode_memory_lookup("linker undefined", idx, 3);
    ASSERT_GT(n, 0);
    ASSERT_INT_EQ(idx[0], 0);
    episode_memory_free();
    return 0;
}
static int t5(void) {
    episode_memory_init();
    episode_memory_record("build", "x", "y", 0, 1);
    int idx[3];
    int n = episode_memory_lookup("zzz_xyzzy_qqq", idx, 3);
    ASSERT_INT_EQ(n, 0);
    episode_memory_free();
    return 0;
}
static int t6(void) {
    episode_memory_init();
    episode_memory_record("compile error missing semicolon", "syntax", "added semi", 0, 1);
    char *p = episode_memory_build_prompt("compile error");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "prior_episodes");
    free(p);
    episode_memory_free();
    return 0;
}
static int t7(void) {
    episode_memory_init();
    char *p = episode_memory_build_prompt("any");
    ASSERT_NOT_NULL(p);
    ASSERT_INT_EQ((int)strlen(p), 0);
    free(p);
    episode_memory_free();
    return 0;
}
static int t8(void) {
    episode_memory_init();
    episode_memory_record("t", "d", "r", 0, 1);
    char *f = episode_memory_format();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Episode Memory");
    free(f);
    episode_memory_free();
    return 0;
}
static int t9(void) {
    episode_memory_init();
    ASSERT_INT_EQ(episode_memory_record(NULL, NULL, NULL, 0, 0), -1);
    ASSERT_INT_EQ(episode_memory_tag(-1, "x"), -1);
    episode_memory_free();
    return 0;
}
static int t10(void) {
    episode_memory_init();
    for (int i = 0; i < 10; i++)
        episode_memory_record("issue", "d", "f", 0, 1);
    ASSERT_INT_EQ(episode_memory_count(), 10);
    episode_memory_free();
    return 0;
}

static int t11_persist_roundtrip(void) {
    char tmp[] = "/tmp/nc_episode_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmp));

    const char *old_home = getenv(ENV_HOME);
    char old_buf[NC_PATH_MAX];
    if (old_home)
        nc_strlcpy(old_buf, old_home, sizeof(old_buf));

    ASSERT_INT_EQ(setenv("HOME", tmp, 1), 0);
    config_home_reset_cache();

    episode_memory_init();
    int idx = episode_memory_record("parser crash", "token mismatch", "normalize scanner", 0, 4);
    ASSERT_INT_EQ(idx, 0);
    ASSERT_INT_EQ(episode_memory_tag(idx, "parser"), 0);
    ASSERT_INT_EQ(episode_memory_save(), 0);
    episode_memory_free();

    episode_memory_init();
    ASSERT_INT_EQ(episode_memory_load(), 1);
    ASSERT_INT_EQ(episode_memory_count(), 1);
    ASSERT_INT_EQ(episode_memory_load(), 1);
    ASSERT_INT_EQ(episode_memory_count(), 1);

    char *prompt = episode_memory_build_prompt("parser");
    ASSERT_NOT_NULL(prompt);
    ASSERT_STR_CONTAINS(prompt, "parser crash");
    ASSERT_STR_CONTAINS(prompt, "normalize scanner");
    free(prompt);
    episode_memory_free();

    char agents[NC_PATH_MAX];
    char epfile[NC_PATH_MAX];
    snprintf(agents, sizeof(agents), "%s/.agents", tmp);
    snprintf(epfile, sizeof(epfile), "%.2045s/%.2045s", agents, EPISODE_PERSIST_FILE);
    unlink(epfile);
    rmdir(agents);
    rmdir(tmp);

    if (old_home)
        ASSERT_INT_EQ(setenv("HOME", old_buf, 1), 0);
    else
        unsetenv("HOME");
    config_home_reset_cache();
    return 0;
}

int test_episode_memory_all(void) {
    TEST_SUITE("episode_memory");
    TEST_COUNTERS;
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
    RUN_TEST(t11_persist_roundtrip);
    TEST_RETURN_FAILED();
}
