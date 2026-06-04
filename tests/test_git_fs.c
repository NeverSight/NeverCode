#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void git_fs_init(void);
extern void git_fs_free(void);
extern int git_fs_load_gitignore(const char *path);
extern bool git_fs_is_ignored(const char *relpath);
extern int git_fs_rule_count(void);
extern bool git_fs_loaded(void);
extern char *git_fs_format(void);

static int test_init_clean(void) {
    git_fs_init();
    ASSERT_FALSE(git_fs_loaded());
    ASSERT_INT_EQ(0, git_fs_rule_count());
    ASSERT_FALSE(git_fs_is_ignored("foo.txt"));
    git_fs_free();
    return 0;
}

static int test_load_and_match(void) {
    const char *tmp = "/tmp/test_gitignore_nc";
    FILE *f = fopen(tmp, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "# comment\n*.o\nbuild/\n!important.o\nnode_modules\n");
    fclose(f);

    git_fs_init();
    int rc = git_fs_load_gitignore(tmp);
    ASSERT_INT_EQ(0, rc);
    ASSERT_TRUE(git_fs_loaded());
    ASSERT_GT(git_fs_rule_count(), 0);

    ASSERT_TRUE(git_fs_is_ignored("main.o"));
    ASSERT_TRUE(git_fs_is_ignored("node_modules"));
    ASSERT_FALSE(git_fs_is_ignored("important.o"));
    ASSERT_FALSE(git_fs_is_ignored("main.c"));

    git_fs_free();
    remove(tmp);
    return 0;
}

static int test_format(void) {
    git_fs_init();
    char *fmt = git_fs_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "GitFS");
    free(fmt);
    git_fs_free();
    return 0;
}

int test_git_fs_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("git_fs");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_load_and_match);
    RUN_TEST(test_format);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("git_fs");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_load_and_match);
    RUN_TEST(test_format);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
