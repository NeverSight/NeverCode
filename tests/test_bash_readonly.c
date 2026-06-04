#include "test.h"
#include "nevercode.h"
#include "cJSON.h"

static int test_readonly_ls(void) {
    ASSERT(bash_is_readonly("ls"));
    ASSERT(bash_is_readonly("ls -la"));
    ASSERT(bash_is_readonly("ls /tmp"));
    ASSERT(bash_is_readonly("ls -la /home/user"));
    return 0;
}

static int test_readonly_cat(void) {
    ASSERT(bash_is_readonly("cat file.txt"));
    ASSERT(bash_is_readonly("head -n 10 file.c"));
    ASSERT(bash_is_readonly("tail -f log.txt"));
    ASSERT(bash_is_readonly("wc -l *.c"));
    return 0;
}

static int test_readonly_git(void) {
    ASSERT(bash_is_readonly("git status"));
    ASSERT(bash_is_readonly("git diff"));
    ASSERT(bash_is_readonly("git log --oneline -20"));
    ASSERT(bash_is_readonly("git branch"));
    ASSERT(bash_is_readonly("git blame file.c"));
    ASSERT(bash_is_readonly("git show HEAD"));
    ASSERT(bash_is_readonly("git tag"));
    ASSERT(bash_is_readonly("git ls-files"));
    ASSERT(bash_is_readonly("git rev-parse HEAD"));
    return 0;
}

static int test_not_readonly_git_write(void) {
    ASSERT(!bash_is_readonly("git commit -m 'test'"));
    ASSERT(!bash_is_readonly("git push origin main"));
    ASSERT(!bash_is_readonly("git checkout -b new-branch"));
    ASSERT(!bash_is_readonly("git add ."));
    ASSERT(!bash_is_readonly("git merge branch"));
    ASSERT(!bash_is_readonly("git -c core.fsmonitor=evil status"));
    return 0;
}

static int test_not_readonly_write_cmds(void) {
    ASSERT(!bash_is_readonly("rm -rf /tmp/dir"));
    ASSERT(!bash_is_readonly("mv a.txt b.txt"));
    ASSERT(!bash_is_readonly("cp src dst"));
    ASSERT(!bash_is_readonly("mkdir newdir"));
    ASSERT(!bash_is_readonly("chmod 755 file"));
    ASSERT(!bash_is_readonly("touch newfile"));
    ASSERT(!bash_is_readonly("sudo apt update"));
    return 0;
}

static int test_not_readonly_shell_ops(void) {
    ASSERT(!bash_is_readonly("echo foo > file.txt"));
    ASSERT(!bash_is_readonly("ls | tee output.txt"));
    ASSERT(!bash_is_readonly("cat file; rm file"));
    ASSERT(!bash_is_readonly("ls && rm -rf /"));
    ASSERT(!bash_is_readonly("echo $(whoami)"));
    ASSERT(!bash_is_readonly("echo `date`"));
    return 0;
}

static int test_readonly_search(void) {
    ASSERT(bash_is_readonly("grep -rn pattern dir/"));
    ASSERT(bash_is_readonly("rg pattern src/"));
    ASSERT(bash_is_readonly("find . -name '*.c'"));
    ASSERT(!bash_is_readonly("find . -exec rm {} +"));
    ASSERT(!bash_is_readonly("find . -delete"));
    return 0;
}

static int test_readonly_misc(void) {
    ASSERT(bash_is_readonly("pwd"));
    ASSERT(bash_is_readonly("whoami"));
    ASSERT(bash_is_readonly("uname -a"));
    ASSERT(bash_is_readonly("date"));
    ASSERT(bash_is_readonly("ps aux"));
    ASSERT(bash_is_readonly("df -h"));
    ASSERT(bash_is_readonly("du -sh ."));
    ASSERT(bash_is_readonly("tree -L 2"));
    ASSERT(bash_is_readonly("jq '.name' package.json"));
    ASSERT(!bash_is_readonly("jq --run-tests file"));
    return 0;
}

static int test_readonly_docker(void) {
    ASSERT(bash_is_readonly("docker ps"));
    ASSERT(bash_is_readonly("docker images"));
    ASSERT(bash_is_readonly("docker logs container1"));
    ASSERT(!bash_is_readonly("docker run ubuntu"));
    ASSERT(!bash_is_readonly("docker exec container1 bash"));
    return 0;
}

static int test_bash_check_readonly_fn(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "git status");
    ASSERT(bash_check_readonly(input) == true);
    cJSON_Delete(input);

    input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "rm -rf /");
    ASSERT(bash_check_readonly(input) == false);
    cJSON_Delete(input);

    ASSERT(bash_check_readonly(NULL) == false);
    return 0;
}

static int test_readonly_empty_null(void) {
    ASSERT(!bash_is_readonly(NULL));
    ASSERT(!bash_is_readonly(""));
    ASSERT(!bash_is_readonly("   "));
    return 0;
}

static int test_readonly_fd(void) {
    ASSERT(bash_is_readonly("fd pattern"));
    ASSERT(bash_is_readonly("fdfind pattern"));
    ASSERT(!bash_is_readonly("fd -x rm {}"));
    ASSERT(!bash_is_readonly("fd --exec cat"));
    return 0;
}

int test_bash_readonly_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("bash_readonly");
    RUN_TEST(test_readonly_ls);
    RUN_TEST(test_readonly_cat);
    RUN_TEST(test_readonly_git);
    RUN_TEST(test_not_readonly_git_write);
    RUN_TEST(test_not_readonly_write_cmds);
    RUN_TEST(test_not_readonly_shell_ops);
    RUN_TEST(test_readonly_search);
    RUN_TEST(test_readonly_misc);
    RUN_TEST(test_readonly_docker);
    RUN_TEST(test_bash_check_readonly_fn);
    RUN_TEST(test_readonly_empty_null);
    RUN_TEST(test_readonly_fd);
    TEST_RETURN_FAILED();
}
