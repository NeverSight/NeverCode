#include "test.h"
#include "tools.h"
#include "harness.h"
#include "defaults.h"
#include <unistd.h>
#include <sys/stat.h>

static char s_td[256];

static void s_mk(void) {
    snprintf(s_td, sizeof(s_td), "/tmp/nc_sc_%d", getpid());
    mkdir(s_td, 0755);
}

static void s_rm(void) {
    nc_rm_rf(s_td);
}

static int t1(void) {
    tool_scaffold_register();
    const tool_def_t *t = tools_find("Scaffold");
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(t->description);
    ASSERT_NOT_NULL(t->execute);
    ASSERT_NOT_NULL(t->input_schema);
    return 0;
}

static int t2(void) {
    s_mk();
    char sv[NC_PATH_MAX];
    ASSERT(getcwd(sv, sizeof(sv)) != NULL);
    ASSERT_INT_EQ(chdir(s_td), 0);

    const tool_def_t *t = tools_find("Scaffold");
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "action", "detect");
    char *o = NULL;
    int r = t->execute(in, &o, NULL);
    ASSERT_INT_EQ(0, r);
    ASSERT_NOT_NULL(o);
    ASSERT_STR_CONTAINS(o, "No known project type");
    free(o);
    cJSON_Delete(in);

    ASSERT_INT_EQ(chdir(sv), 0);
    s_rm();
    return 0;
}

static int t3(void) {
    s_mk();
    char p[512];
    snprintf(p, sizeof(p), "%s/package.json", s_td);
    FILE *f = fopen(p, "w");
    fprintf(f, "{}");
    fclose(f);

    char sv[NC_PATH_MAX];
    ASSERT(getcwd(sv, sizeof(sv)) != NULL);
    ASSERT_INT_EQ(chdir(s_td), 0);

    const tool_def_t *t = tools_find("Scaffold");
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "action", "detect");
    char *o = NULL;
    int r = t->execute(in, &o, NULL);
    ASSERT_INT_EQ(0, r);
    ASSERT_STR_CONTAINS(o, "node");
    free(o);
    cJSON_Delete(in);

    ASSERT_INT_EQ(chdir(sv), 0);
    s_rm();
    return 0;
}

static int t4(void) {
    s_mk();
    char sv[NC_PATH_MAX];
    ASSERT(getcwd(sv, sizeof(sv)) != NULL);
    ASSERT_INT_EQ(chdir(s_td), 0);

    const tool_def_t *t = tools_find("Scaffold");
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "action", "status");
    char *o = NULL;
    int r = t->execute(in, &o, NULL);
    ASSERT_INT_EQ(0, r);
    ASSERT_STR_CONTAINS(o, "Status:");
    ASSERT_STR_CONTAINS(o, "Recipes available:");
    free(o);
    cJSON_Delete(in);

    ASSERT_INT_EQ(chdir(sv), 0);
    s_rm();
    return 0;
}

static int t5(void) {
    s_mk();
    char sv[NC_PATH_MAX];
    ASSERT(getcwd(sv, sizeof(sv)) != NULL);
    ASSERT_INT_EQ(chdir(s_td), 0);

    const tool_def_t *t = tools_find("Scaffold");
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "action", "bogus");
    char *o = NULL;
    int r = t->execute(in, &o, NULL);
    ASSERT_INT_EQ(0, r);
    ASSERT_STR_CONTAINS(o, "Unknown action");
    free(o);
    cJSON_Delete(in);

    ASSERT_INT_EQ(chdir(sv), 0);
    s_rm();
    return 0;
}

static int t6(void) {
    s_mk();
    char sv[NC_PATH_MAX];
    ASSERT(getcwd(sv, sizeof(sv)) != NULL);
    ASSERT_INT_EQ(chdir(s_td), 0);

    const tool_def_t *t = tools_find("Scaffold");
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "action", "script");
    char *o = NULL;
    int r = t->execute(in, &o, NULL);
    ASSERT_INT_EQ(0, r);
    ASSERT_STR_CONTAINS(o, "init.sh");
    free(o);
    cJSON_Delete(in);

    ASSERT_INT_EQ(chdir(sv), 0);
    s_rm();
    return 0;
}

int test_scaffold_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("scaffold");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    TEST_RETURN_FAILED();
}
