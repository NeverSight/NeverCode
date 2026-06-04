/*
 * test_async_watcher.c
 */

#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int test_child_output(void) {
    int fd[2];
    ASSERT_INT_EQ(pipe(fd), 0);
    pid_t pid = fork();
    ASSERT((int)pid >= 0);
    if (pid == 0) {
        close(fd[0]);
        (void)write(fd[1], "out", 3);
        close(fd[1]);
        _exit(19);
    }
    close(fd[1]);
    async_watcher_init();
    int ix = async_watcher_add(pid, fd[0], -1, 8000, "child");
    ASSERT(ix >= 0);
    while (!async_watcher_done(ix))
        async_watcher_poll();
    ASSERT_INT_EQ(async_watcher_exit_code(ix), 19);
    char *o = async_watcher_output(ix);
    ASSERT_NOT_NULL(o);
    ASSERT_INT_EQ((int)strlen(o), 3);
    ASSERT_STR_EQ(o, "out");
    free(o);
    async_watcher_free();
    return 0;
}

int test_async_watcher_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("async_watcher");
    RUN_TEST(test_child_output);
    TEST_RETURN_FAILED();
}
