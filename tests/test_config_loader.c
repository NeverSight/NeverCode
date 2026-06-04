/*
 * test_config_loader.c
 */

#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_file(const char *path, const char *body) {
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fputs(body, f);
    fclose(f);
    return 0;
}

static int test_merge_layers(void) {
    char tpl[] = "/tmp/nc_cfg_XXXXXX";
    ASSERT(mkdtemp(tpl) != NULL);
    char d_nev[512], d_ag[512], j1[512], j2[512];
    snprintf(d_nev, sizeof d_nev, "%s%s", tpl, CONFIG_DIR_SECONDARY);
    snprintf(d_ag, sizeof d_ag, "%s%s", tpl, CONFIG_DIR_PRIMARY);
    ASSERT_INT_EQ(mkdir(d_nev, 0755), 0);
    ASSERT_INT_EQ(mkdir(d_ag, 0755), 0);
    snprintf(j1, sizeof j1, "%s%s", d_nev, NC_LAYER_CONFIG_FILE);
    snprintf(j2, sizeof j2, "%s%s", d_ag, NC_LAYER_CONFIG_FILE);
    ASSERT_INT_EQ(write_file(j1, "{\"a\":1,\"nest\":{\"x\":1}}"), 0);
    ASSERT_INT_EQ(write_file(j2, "{\"b\":2,\"nest\":{\"y\":2}}"), 0);

    config_loader_init();
    ASSERT_INT_EQ(config_loader_load_all(tpl), 2);
    ASSERT_INT_EQ(config_loader_get_int("a", -1), 1);
    ASSERT_INT_EQ(config_loader_get_int("b", -1), 2);
    ASSERT_NOT_NULL(config_loader_merged());
    char *fmt = config_loader_format();
    ASSERT_NOT_NULL(fmt);
    free(fmt);
    config_loader_free();

    nc_rm_rf(tpl);
    return 0;
}

static int test_reload_stale(void) {
    char tpl[] = "/tmp/nc_cfg2_XXXXXX";
    ASSERT(mkdtemp(tpl) != NULL);
    char d[512], j[512];
    snprintf(d, sizeof d, "%s%s", tpl, CONFIG_DIR_SECONDARY);
    ASSERT_INT_EQ(mkdir(d, 0755), 0);
    snprintf(j, sizeof j, "%s%s", d, NC_LAYER_CONFIG_FILE);
    ASSERT_INT_EQ(write_file(j, "{\"k\":1}"), 0);
    config_loader_init();
    config_loader_load_all(tpl);
    ASSERT_INT_EQ(config_loader_get_int("k", 0), 1);
    ASSERT_INT_EQ(config_loader_reload_if_stale(tpl), 0);
    sleep(1);
    ASSERT_INT_EQ(write_file(j, "{\"k\":2}"), 0);
    ASSERT_INT_EQ(config_loader_reload_if_stale(tpl), 1);
    ASSERT_INT_EQ(config_loader_get_int("k", 0), 2);
    config_loader_free();
    nc_rm_rf(tpl);
    return 0;
}

static int test_loads_home_layers(void) {
    char project_tpl[] = "/tmp/nc_cfg_proj_XXXXXX";
    char home_tpl[] = "/tmp/nc_cfg_home_XXXXXX";
    ASSERT(mkdtemp(project_tpl) != NULL);
    ASSERT(mkdtemp(home_tpl) != NULL);

    char d_primary[512], d_secondary[512], d_compat[512];
    char j_primary[512], j_secondary[512], j_compat[512];
    snprintf(d_primary, sizeof d_primary, "%s%s", home_tpl, CONFIG_DIR_PRIMARY);
    snprintf(d_secondary, sizeof d_secondary, "%s%s", home_tpl, CONFIG_DIR_SECONDARY);
    snprintf(d_compat, sizeof d_compat, "%s%s", home_tpl, CONFIG_DIR_COMPAT);
    ASSERT_INT_EQ(mkdir(d_primary, 0755), 0);
    ASSERT_INT_EQ(mkdir(d_secondary, 0755), 0);
    ASSERT_INT_EQ(mkdir(d_compat, 0755), 0);

    snprintf(j_primary, sizeof j_primary, "%s%s", d_primary, NC_LAYER_CONFIG_FILE);
    snprintf(j_secondary, sizeof j_secondary, "%s%s", d_secondary, NC_LAYER_CONFIG_FILE);
    snprintf(j_compat, sizeof j_compat, "%s%s", d_compat, NC_LAYER_CONFIG_FILE);
    ASSERT_INT_EQ(write_file(j_primary, "{\"from_primary\":11}"), 0);
    ASSERT_INT_EQ(write_file(j_secondary, "{\"from_secondary\":22}"), 0);
    ASSERT_INT_EQ(write_file(j_compat, "{\"from_compat\":33}"), 0);

    const char *home_old = getenv("HOME");
    char *home_backup = home_old ? nc_xstrdup(home_old) : NULL;
    ASSERT_INT_EQ(setenv("HOME", home_tpl, 1), 0);

    config_loader_init();
    ASSERT_INT_EQ(config_loader_load_all(project_tpl), 3);
    ASSERT_INT_EQ(config_loader_get_int("from_primary", 0), 11);
    ASSERT_INT_EQ(config_loader_get_int("from_secondary", 0), 22);
    ASSERT_INT_EQ(config_loader_get_int("from_compat", 0), 33);
    config_loader_free();

    if (home_backup) {
        ASSERT_INT_EQ(setenv("HOME", home_backup, 1), 0);
        free(home_backup);
    } else {
        ASSERT_INT_EQ(unsetenv("HOME"), 0);
    }

    nc_rm_rf(project_tpl);
    nc_rm_rf(home_tpl);
    return 0;
}

static int test_project_overrides_home_layers(void) {
    char project_tpl[] = "/tmp/nc_cfg_proj_override_XXXXXX";
    char home_tpl[] = "/tmp/nc_cfg_home_override_XXXXXX";
    ASSERT(mkdtemp(project_tpl) != NULL);
    ASSERT(mkdtemp(home_tpl) != NULL);

    char home_dir[512], proj_dir[512];
    char home_json[512], proj_json[512];
    snprintf(home_dir, sizeof home_dir, "%s%s", home_tpl, CONFIG_DIR_PRIMARY);
    snprintf(proj_dir, sizeof proj_dir, "%s%s", project_tpl, CONFIG_DIR_PRIMARY);
    ASSERT_INT_EQ(mkdir(home_dir, 0755), 0);
    ASSERT_INT_EQ(mkdir(proj_dir, 0755), 0);

    snprintf(home_json, sizeof home_json, "%s%s", home_dir, NC_LAYER_CONFIG_FILE);
    snprintf(proj_json, sizeof proj_json, "%s%s", proj_dir, NC_LAYER_CONFIG_FILE);
    ASSERT_INT_EQ(write_file(home_json, "{\"shared\":1,\"home_only\":2}"), 0);
    ASSERT_INT_EQ(write_file(proj_json, "{\"shared\":9,\"project_only\":3}"), 0);

    const char *home_old = getenv("HOME");
    char *home_backup = home_old ? nc_xstrdup(home_old) : NULL;
    ASSERT_INT_EQ(setenv("HOME", home_tpl, 1), 0);

    config_loader_init();
    ASSERT_INT_EQ(config_loader_load_all(project_tpl), 2);
    ASSERT_INT_EQ(config_loader_get_int("shared", 0), 9);
    ASSERT_INT_EQ(config_loader_get_int("home_only", 0), 2);
    ASSERT_INT_EQ(config_loader_get_int("project_only", 0), 3);
    config_loader_free();

    if (home_backup) {
        ASSERT_INT_EQ(setenv("HOME", home_backup, 1), 0);
        free(home_backup);
    } else {
        ASSERT_INT_EQ(unsetenv("HOME"), 0);
    }

    nc_rm_rf(project_tpl);
    nc_rm_rf(home_tpl);
    return 0;
}

int test_config_loader_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("config_loader");
    RUN_TEST(test_merge_layers);
    RUN_TEST(test_reload_stale);
    RUN_TEST(test_loads_home_layers);
    RUN_TEST(test_project_overrides_home_layers);
    TEST_RETURN_FAILED();
}
