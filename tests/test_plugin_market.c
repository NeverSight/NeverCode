#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

int plugin_market_scan(const char *project_root);
int plugin_market_count(void);
int plugin_market_marketplace_count(void);
const char *plugin_market_plugin_name(int market_idx, int plugin_idx);
char *plugin_market_format_list(void);


static const char *SHA64 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static int _mk_marketplace(char *home, const char *manifest) {
    char plugins[NC_PATH_MAX];
    snprintf(plugins, sizeof(plugins), "%s/.agents/plugins", home);
    char cmd[NC_PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", plugins);
    int _mrc = system(cmd);
    if (_mrc != 0)
        return -1;
    char mpath[NC_PATH_MAX];
    snprintf(mpath, sizeof(mpath), "%.4078s/marketplace.json", plugins);
    FILE *f = fopen(mpath, "w");
    if (!f)
        return -1;
    fputs(manifest, f);
    fclose(f);
    return 0;
}

static char *_with_temp_home(void) {
    char tmpl[] = "/tmp/nevercode_test_home_XXXXXX";
    char *dir = mkdtemp(tmpl);
    return dir ? nc_xstrdup(dir) : NULL;
}

static int test_marketplace_fail_closed(void) {
    char *home = _with_temp_home();
    ASSERT_NOT_NULL(home);
    const char *old_home = getenv("HOME");
    char *backup = old_home ? nc_xstrdup(old_home) : NULL;

    char manifest[4096];
    snprintf(manifest, sizeof(manifest),
             "{\"name\":\"testmkt\",\"plugins\":["
             "{\"name\":\"good-url\",\"version\":\"1.0.0\",\"source\":{\"url\":\"https://example.com/p.so\",\"sha256\":\"%s\"}},"
             "{\"name\":\"bad-url-no-sha\",\"source\":{\"url\":\"https://example.com/q.so\"}},"
             "{\"name\":\"bad-url-cleartext\",\"source\":{\"url\":\"http://example.com/r.so\",\"sha256\":\"%s\"}},"
             "{\"name\":\"good-git\",\"source\":{\"git\":\"git@github.com:x/y.git\",\"sha256\":\"%s\"}}"
             "]}",
             SHA64, SHA64, SHA64);

    ASSERT_INT_EQ(_mk_marketplace(home, manifest), 0);
    nc_home_dir_reset();
    setenv("HOME", home, 1);
    nc_home_dir_reset();

    int n = plugin_market_scan("/tmp/nevercode_test_no_proj_root");
    /* Only good-url and good-git survive validation */
    ASSERT_INT_EQ(n, 2);
    ASSERT_INT_EQ(plugin_market_count(), 2);
    ASSERT(plugin_market_plugin_sha256(0, 0) != NULL);
    ASSERT_STR_EQ(plugin_market_plugin_sha256(0, 0), SHA64);

    char dcmd[NC_PATH_MAX * 2];
    snprintf(dcmd, sizeof(dcmd), "rm -rf '%s'", home);
    if (backup) {
        setenv("HOME", backup, 1);
        free(backup);
    } else {
        unsetenv("HOME");
    }
    nc_home_dir_reset();
    int _drc = system(dcmd);
    if (_drc != 0) {
        /* cleanup best effort */
    }
    free(home);
    return 0;
}

static int test_marketplace_local_source_ok(void) {
    char *home = _with_temp_home();
    ASSERT_NOT_NULL(home);
    const char *old_home = getenv("HOME");
    char *backup = old_home ? nc_xstrdup(old_home) : NULL;

    const char *manifest =
        "{\"name\":\"testmkt\",\"plugins\":["
        "{\"name\":\"local-plug\",\"source\":{\"local\":\"/opt/x/p.so\"}}"
        "]}";
    ASSERT_INT_EQ(_mk_marketplace(home, manifest), 0);
    nc_home_dir_reset();
    setenv("HOME", home, 1);
    nc_home_dir_reset();

    int n = plugin_market_scan("/tmp/nevercode_test_no_proj_root_2");
    ASSERT_INT_EQ(n, 1);
    ASSERT_NULL(plugin_market_plugin_sha256(0, 0));

    char dcmd[NC_PATH_MAX * 2];
    snprintf(dcmd, sizeof(dcmd), "rm -rf '%s'", home);
    if (backup) {
        setenv("HOME", backup, 1);
        free(backup);
    } else {
        unsetenv("HOME");
    }
    nc_home_dir_reset();
    int _drc = system(dcmd);
    if (_drc != 0) {
        /* cleanup best effort */
    }
    free(home);
    return 0;
}

static int test_scan_empty(void) {
    int r = plugin_market_scan("/tmp/nevercode_test_no_plugins");
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(plugin_market_count(), 0);
    ASSERT_INT_EQ(plugin_market_marketplace_count(), 0);
    return 0;
}

static int test_format_empty(void) {
    plugin_market_scan("/tmp/nonexistent");
    char *s = plugin_market_format_list();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "No plugin");
    free(s);
    return 0;
}

static int test_invalid_indices(void) {
    ASSERT_NULL(plugin_market_plugin_name(-1, 0));
    ASSERT_NULL(plugin_market_plugin_name(0, -1));
    ASSERT_NULL(plugin_market_plugin_name(999, 0));
    return 0;
}

int test_plugin_market_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("plugin_market");
    RUN_TEST(test_scan_empty);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_invalid_indices);
    RUN_TEST(test_marketplace_fail_closed);
    RUN_TEST(test_marketplace_local_source_ok);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("plugin_market");
    RUN_TEST(test_scan_empty);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_invalid_indices);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
