#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static int test_procgroup_bash_echo(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "echo pgid_test_ok");
    char *output = NULL;
    int rc = tools_find("Bash")->execute(input, &output, NULL);
    ASSERT(rc == 0);
    ASSERT(output != NULL);
    ASSERT(strstr(output, "pgid_test_ok") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_procgroup_timeout_kills(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "sleep 60");
    cJSON_AddNumberToObject(input, "timeout", 1);
    char *output = NULL;
    int rc = tools_find("Bash")->execute(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT(output != NULL);
    ASSERT(strstr(output, "[timed out]") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_procgroup_no_zombies(void) {
    /* Drain any lingering zombies from prior tests */
    int wst;
    while (waitpid(-1, &wst, WNOHANG) > 0) {
    }

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "echo clean_test");
    char *output = NULL;
    int rc = tools_find("Bash")->execute(input, &output, NULL);
    ASSERT(rc == 0);
    ASSERT(strstr(output, "clean_test") != NULL);

    /* After a simple echo, there should be no zombies left */
    pid_t z = waitpid(-1, &wst, WNOHANG);
    ASSERT(z <= 0);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_procgroup_subshell(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "(echo a && echo b)");
    char *output = NULL;
    int rc = tools_find("Bash")->execute(input, &output, NULL);
    ASSERT(rc == 0);
    ASSERT(strstr(output, "a") != NULL);
    ASSERT(strstr(output, "b") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_procgroup_exit_code(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "exit 42");
    char *output = NULL;
    int rc = tools_find("Bash")->execute(input, &output, NULL);
    ASSERT(rc == 42);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_procgroup_workdir(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "pwd");
    cJSON_AddStringToObject(input, "working_directory", "/tmp");
    char *output = NULL;
    int rc = tools_find("Bash")->execute(input, &output, NULL);
    ASSERT(rc == 0);
    ASSERT(strstr(output, "/tmp") != NULL || strstr(output, "/private/tmp") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_procgroup_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("procgroup");

    RUN_TEST(test_procgroup_bash_echo);
    RUN_TEST(test_procgroup_timeout_kills);
    RUN_TEST(test_procgroup_no_zombies);
    RUN_TEST(test_procgroup_subshell);
    RUN_TEST(test_procgroup_exit_code);
    RUN_TEST(test_procgroup_workdir);

    TEST_RETURN_FAILED();
}
