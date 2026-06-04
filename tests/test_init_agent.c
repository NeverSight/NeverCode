#include "test.h"
#include "harness.h"
#include "defaults.h"
#include <sys/stat.h>
#include <unistd.h>

static char s_td[256];

static void s_mk(void) {
    snprintf(s_td, sizeof(s_td), "/tmp/nc_ia_%d", getpid());
    mkdir(s_td, 0755);
}

static void s_rm(void) {
    nc_rm_rf(s_td);
}

static int t1(void) {
    s_mk();
    ASSERT_TRUE(init_agent_needed(s_td));
    s_rm();
    return 0;
}

static int t2(void) {
    ASSERT_FALSE(init_agent_needed(NULL));
    ASSERT_FALSE(init_agent_needed(""));
    return 0;
}

static int t3(void) {
    s_mk();
    char d[512];
    snprintf(d, sizeof(d), "%s/.agents", s_td);
    mkdir(d, 0755);
    char m[512];
    snprintf(m, sizeof(m), "%s/.agents/.init_done", s_td);
    FILE *f = fopen(m, "w");
    fprintf(f, "1\n");
    fclose(f);
    ASSERT_FALSE(init_agent_needed(s_td));
    s_rm();
    return 0;
}

static int t4(void) {
    s_mk();
    ASSERT_INT_EQ(-1, init_agent_detect_type(s_td));
    s_rm();
    return 0;
}

static int t5(void) {
    s_mk();
    char p[512];
    snprintf(p, sizeof(p), "%s/package.json", s_td);
    FILE *f = fopen(p, "w");
    fprintf(f, "{}");
    fclose(f);
    int i = init_agent_detect_type(s_td);
    ASSERT_GE(i, 0);
    ASSERT_STR_EQ("node", init_agent_type_name(i));
    ASSERT_NOT_NULL(init_agent_run_cmd(i));
    ASSERT_NOT_NULL(init_agent_test_cmd(i));
    s_rm();
    return 0;
}

static int t6(void) {
    s_mk();
    char p[512];
    snprintf(p, sizeof(p), "%s/Makefile", s_td);
    FILE *f = fopen(p, "w");
    fprintf(f, "all:\n");
    fclose(f);
    int i = init_agent_detect_type(s_td);
    ASSERT_GE(i, 0);
    ASSERT_STR_EQ("c", init_agent_type_name(i));
    s_rm();
    return 0;
}

static int t7(void) {
    s_mk();
    ASSERT_INT_EQ(0, init_agent_gen_script(s_td, 0));
    char p[512];
    snprintf(p, sizeof(p), "%s/init.sh", s_td);
    struct stat st;
    ASSERT_INT_EQ(0, stat(p, &st));
    ASSERT_TRUE(st.st_mode & S_IXUSR);
    s_rm();
    return 0;
}

static int t8(void) {
    s_mk();
    ASSERT_INT_EQ(0, init_agent_gen_script(s_td, -1));
    s_rm();
    return 0;
}

static int t9(void) {
    s_mk();
    ASSERT_INT_EQ(0, init_agent_write_marker(s_td));
    ASSERT_FALSE(init_agent_needed(s_td));
    s_rm();
    return 0;
}

static int t10(void) {
    s_mk();
    char *c = init_agent_build_context(s_td, -1);
    ASSERT_NOT_NULL(c);
    ASSERT_STR_CONTAINS(c, "initializer_context");
    ASSERT_STR_CONTAINS(c, "Workflow");
    free(c);
    s_rm();
    return 0;
}

static int t11(void) {
    s_mk();
    char *c = init_agent_build_context(s_td, 0);
    ASSERT_NOT_NULL(c);
    ASSERT_STR_CONTAINS(c, "node");
    free(c);
    s_rm();
    return 0;
}

static int t12(void) {
    s_mk();
    progress_tracker_init();
    ASSERT_TRUE(init_agent_needed(s_td));
    int recipe = -1;
    int r = init_agent_run(s_td, &recipe);
    ASSERT_INT_EQ(1, r);
    ASSERT_FALSE(init_agent_needed(s_td));
    s_rm();
    return 0;
}

static int t13(void) {
    s_mk();
    init_agent_write_marker(s_td);
    int recipe = -1;
    int r = init_agent_run(s_td, &recipe);
    ASSERT_INT_EQ(0, r);
    ASSERT_INT_EQ(-1, recipe);
    s_rm();
    return 0;
}

static int t18(void) {
    s_mk();
    char p[512];
    snprintf(p, sizeof(p), "%s/Cargo.toml", s_td);
    FILE *f = fopen(p, "w");
    fprintf(f, "[package]");
    fclose(f);

    progress_tracker_init();
    int recipe = -1;
    int r = init_agent_run(s_td, &recipe);
    ASSERT_INT_EQ(1, r);
    ASSERT_GE(recipe, 0);
    ASSERT_STR_EQ("rust", init_agent_type_name(recipe));
    s_rm();
    return 0;
}

static int t14(void) {
    ASSERT_GT(init_agent_recipe_count(), 5);
    return 0;
}

static int t15(void) {
    ASSERT_STR_EQ("unknown", init_agent_type_name(-1));
    ASSERT_STR_EQ("unknown", init_agent_type_name(999));
    return 0;
}

static int t16(void) {
    ASSERT_NULL(init_agent_run_cmd(-1));
    ASSERT_NULL(init_agent_test_cmd(-1));
    return 0;
}

static int t17(void) {
    s_mk();
    char p[512];
    snprintf(p, sizeof(p), "%s/App.csproj", s_td);
    FILE *f = fopen(p, "w");
    fprintf(f, "<Project></Project>");
    fclose(f);
    int i = init_agent_detect_type(s_td);
    ASSERT_GE(i, 0);
    ASSERT_STR_EQ("dotnet", init_agent_type_name(i));
    s_rm();
    return 0;
}

int test_init_agent_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("init_agent");
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
    RUN_TEST(t11);
    RUN_TEST(t12);
    RUN_TEST(t13);
    RUN_TEST(t14);
    RUN_TEST(t15);
    RUN_TEST(t16);
    RUN_TEST(t17);
    RUN_TEST(t18);
    TEST_RETURN_FAILED();
}
