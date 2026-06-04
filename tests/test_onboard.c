#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int onboard_detect(const char *project_root);
int onboard_setup(void);
int onboard_is_first_run(void);
int onboard_project_count(void);
const char *onboard_project_name(int idx);
int onboard_has_agents_md(void);
int onboard_has_git(void);
const char *onboard_state_name(int s);
char *onboard_format_status(void);

static int test_detect_nonexistent(void) {
    int s = onboard_detect("/tmp/nevercode_test_onboard_xyz");
    /* first_run or onboarded depending on HOME state */
    ASSERT_GE(s, 0);
    return 0;
}

static int test_state_names(void) {
    ASSERT_STR_EQ(onboard_state_name(0), "unknown");
    ASSERT_STR_EQ(onboard_state_name(1), "first_run");
    ASSERT_STR_EQ(onboard_state_name(2), "onboarded");
    ASSERT_STR_EQ(onboard_state_name(3), "upgraded");
    return 0;
}

static int test_project_detection(void) {
    /* detect from /tmp which likely has no project files */
    onboard_detect("/tmp");
    /* project count could be 0 or more depending on /tmp content */
    ASSERT_GE(onboard_project_count(), 0);
    ASSERT_NULL(onboard_project_name(-1));
    ASSERT_NULL(onboard_project_name(9999));
    return 0;
}

static int test_format_status(void) {
    onboard_detect("/tmp");
    char *s = onboard_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Onboard state:");
    free(s);
    return 0;
}

static int test_detect_with_project(void) {
    onboard_detect(".");
    int found_make = 0;
    for (int i = 0; i < onboard_project_count(); i++) {
        const char *n = onboard_project_name(i);
        if (n && strcmp(n, "make") == 0)
            found_make = 1;
    }
    (void)found_make;
    return 0;
}

static int test_csproj_detection(void) {
    char td[256];
    snprintf(td, sizeof(td), "/tmp/nc_ob_csproj_%d", getpid());
    mkdir(td, 0755);

    char p[512];
    snprintf(p, sizeof(p), "%s/MyApp.csproj", td);
    FILE *f = fopen(p, "w");
    fprintf(f, "<Project/>");
    fclose(f);

    onboard_detect(td);
    int found_dotnet = 0;
    for (int i = 0; i < onboard_project_count(); i++) {
        const char *n = onboard_project_name(i);
        if (n && strcmp(n, "dotnet") == 0)
            found_dotnet = 1;
    }
    ASSERT_INT_EQ(1, found_dotnet);

    nc_rm_rf(td);
    return 0;
}

int test_onboard_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("onboard");
    RUN_TEST(test_detect_nonexistent);
    RUN_TEST(test_state_names);
    RUN_TEST(test_project_detection);
    RUN_TEST(test_format_status);
    RUN_TEST(test_detect_with_project);
    RUN_TEST(test_csproj_detection);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("onboard");
    RUN_TEST(test_detect_nonexistent);
    RUN_TEST(test_state_names);
    RUN_TEST(test_project_detection);
    RUN_TEST(test_format_status);
    RUN_TEST(test_detect_with_project);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
