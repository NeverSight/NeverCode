/*
 * test_skills_watcher.c
 */

#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int writep(const char *path, const char *t) {
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fputs(t, f);
    fclose(f);
    return 0;
}

static int test_track_and_format(void) {
    char dir[] = "/tmp/nc_sk_XXXXXX";
    ASSERT(mkdtemp(dir) != NULL);
    char md[512];
    snprintf(md, sizeof md, "%s/a.md", dir);
    ASSERT_INT_EQ(writep(md, "# Skill A\n"), 0);

    skills_watcher_init();
    ASSERT(skills_watcher_add_dir(dir) >= 1);
    ASSERT(skills_watcher_count() >= 1);
    char *f = skills_watcher_format();
    ASSERT_NOT_NULL(f);
    ASSERT(strstr(f, "Skill") != NULL);
    free(f);
    skills_watcher_free();

    nc_rm_rf(dir);
    return 0;
}

int test_skills_watcher_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("skills_watcher");
    RUN_TEST(test_track_and_format);
    TEST_RETURN_FAILED();
}
