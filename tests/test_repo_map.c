#include "test.h"
#include "harness.h"
#include "defaults.h"
#include <sys/stat.h>
#include <unistd.h>
static int t_rmap_null(void) {
    char *r = repo_map_generate(NULL, 0);
    ASSERT_NULL(r);
    ASSERT_INT_EQ(repo_map_entry_count(NULL), 0);
    return 0;
}
static int t_rmap_nodir(void) {
    char *r = repo_map_generate("/nonexistent/xyz", 0);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "Files: 0");
    free(r);
    return 0;
}
static int t_rmap_gen(void) {
    mkdir("/tmp/nc_rm1", 0755);
    FILE *f = fopen("/tmp/nc_rm1/a.c", "w");
    if (f) {
        fprintf(f, "x\n");
        fclose(f);
    }
    char *r = repo_map_generate("/tmp/nc_rm1", 0);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_CONTAINS(r, "Repository Map");
    free(r);
    remove("/tmp/nc_rm1/a.c");
    rmdir("/tmp/nc_rm1");
    return 0;
}
static int t_rmap_sum(void) {
    mkdir("/tmp/nc_rm2", 0755);
    FILE *f = fopen("/tmp/nc_rm2/a.txt", "w");
    if (f) {
        fprintf(f, "x\n");
        fclose(f);
    }
    char *s = repo_map_summary("/tmp/nc_rm2");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "1 files");
    free(s);
    remove("/tmp/nc_rm2/a.txt");
    rmdir("/tmp/nc_rm2");
    return 0;
}
static int t_rmap_cnt(void) {
    mkdir("/tmp/nc_rm3", 0755);
    FILE *f = fopen("/tmp/nc_rm3/x.c", "w");
    if (f) {
        fprintf(f, "x\n");
        fclose(f);
    }
    int n = repo_map_entry_count("/tmp/nc_rm3");
    ASSERT_GE(n, 1);
    remove("/tmp/nc_rm3/x.c");
    rmdir("/tmp/nc_rm3");
    return 0;
}
static int t_rmap_skip(void) {
    mkdir("/tmp/nc_rm4", 0755);
    mkdir("/tmp/nc_rm4/.hid", 0755);
    FILE *f = fopen("/tmp/nc_rm4/.hid/s.txt", "w");
    if (f) {
        fprintf(f, "s\n");
        fclose(f);
    }
    f = fopen("/tmp/nc_rm4/v.txt", "w");
    if (f) {
        fprintf(f, "v\n");
        fclose(f);
    }
    char *r = repo_map_generate("/tmp/nc_rm4", 0);
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "s.txt") == NULL);
    free(r);
    remove("/tmp/nc_rm4/.hid/s.txt");
    rmdir("/tmp/nc_rm4/.hid");
    remove("/tmp/nc_rm4/v.txt");
    rmdir("/tmp/nc_rm4");
    return 0;
}
int test_repo_map_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("repo_map");
    RUN_TEST(t_rmap_null);
    RUN_TEST(t_rmap_nodir);
    RUN_TEST(t_rmap_gen);
    RUN_TEST(t_rmap_sum);
    RUN_TEST(t_rmap_cnt);
    RUN_TEST(t_rmap_skip);
    TEST_RETURN_FAILED();
}
