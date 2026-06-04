#include "test.h"
#include "nevercode.h"
#include <sys/stat.h>
#include <unistd.h>

int config_migrate_run(const char *home);
const char *config_migrate_status_name(int s);

static int test_migrate_null_home(void) {
    int r = config_migrate_run(NULL);
    ASSERT_INT_EQ(r, -1);
    return 0;
}

static int test_migrate_empty_home(void) {
    int r = config_migrate_run("");
    ASSERT_INT_EQ(r, -1);
    return 0;
}

static int test_migrate_nonexistent(void) {
    int r = config_migrate_run("/tmp/nevercode_test_nonexistent_dir_xyz");
    ASSERT_GE(r, 0);
    return 0;
}

static int test_status_names(void) {
    ASSERT_STR_EQ(config_migrate_status_name(0), "applied");
    ASSERT_STR_EQ(config_migrate_status_name(1), "skipped_marker");
    ASSERT_STR_EQ(config_migrate_status_name(2), "skipped_no_data");
    ASSERT_STR_EQ(config_migrate_status_name(3), "skipped_explicit");
    ASSERT_STR_EQ(config_migrate_status_name(4), "error");
    ASSERT_STR_EQ(config_migrate_status_name(99), "unknown");
    return 0;
}

static int test_compat_dirs_copies_config(void) {
    char base[256];
    snprintf(base, sizeof(base), "/tmp/nc_cm_%d", getpid());
    nc_rm_rf(base);
    ASSERT_INT_EQ(0, mkdir(base, 0755));

    char cld[320];
    snprintf(cld, sizeof(cld), "%s/.claude", base);
    ASSERT_INT_EQ(0, mkdir(cld, 0755));
    char cfg[320];
    snprintf(cfg, sizeof(cfg), "%s/config.json", cld);
    FILE *wf = fopen(cfg, "w");
    ASSERT_NOT_NULL(wf);
    fprintf(wf, "{\"x\":1}");
    fclose(wf);

    int r = config_migrate_run(base);
    ASSERT_GE(r, 0);

    char dest[320];
    snprintf(dest, sizeof(dest), "%s/.agents/config.json", base);
    struct stat st;
    ASSERT_INT_EQ(0, stat(dest, &st));

    char *buf = strbuf_read_file(dest, NULL);
    ASSERT_NOT_NULL(buf);
    ASSERT_STR_EQ(buf, "{\"x\":1}");
    free(buf);

    nc_rm_rf(base);
    return 0;
}

int test_config_migrate_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("config_migrate");
    RUN_TEST(test_migrate_null_home);
    RUN_TEST(test_migrate_empty_home);
    RUN_TEST(test_migrate_nonexistent);
    RUN_TEST(test_status_names);
    RUN_TEST(test_compat_dirs_copies_config);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("config_migrate");
    RUN_TEST(test_migrate_null_home);
    RUN_TEST(test_migrate_empty_home);
    RUN_TEST(test_migrate_nonexistent);
    RUN_TEST(test_status_names);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
