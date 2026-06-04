#include "test.h"
#include "nevercode.h"
#include <unistd.h>

static const char *tmpfile_path = "/tmp/nevercode_test_traj_input.jsonl";
static const char *outfile_path = "/tmp/nevercode_test_traj_output.jsonl";

static void write_test_trajectory(int n_entries) {
    FILE *f = fopen(tmpfile_path, "w");
    for (int i = 0; i < n_entries; i++) {
        const char *role = (i % 3 == 0) ? "user" : (i % 3 == 1) ? "assistant" : "tool";
        fprintf(f, "{\"role\":\"%s\",\"content\":\"message %d\"}\n", role, i);
    }
    fclose(f);
}

static int count_lines(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int n = 0;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f))
        n++;
    fclose(f);
    return n;
}

static int t_short_no_compress(void) {
    write_test_trajectory(10);
    int rc = traj_compress_file(tmpfile_path, outfile_path);
    ASSERT_INT_EQ(rc, 0);
    unlink(tmpfile_path);
    return 0;
}

static int t_long_compress(void) {
    write_test_trajectory(60);
    int rc = traj_compress_file(tmpfile_path, outfile_path);
    ASSERT_GT(rc, 0);
    int lines = count_lines(outfile_path);
    ASSERT_GT(lines, 0);
    ASSERT(lines < 60);
    unlink(tmpfile_path);
    unlink(outfile_path);
    return 0;
}

static int t_null_inputs(void) {
    ASSERT_INT_EQ(traj_compress_file(NULL, NULL), -1);
    ASSERT_INT_EQ(traj_compress_file("/nonexistent", outfile_path), -1);
    return 0;
}

static int t_preserves_first(void) {
    write_test_trajectory(50);
    int rc = traj_compress_file(tmpfile_path, outfile_path);
    ASSERT_GT(rc, 0);

    FILE *f = fopen(outfile_path, "r");
    ASSERT_NOT_NULL(f);
    char first_line[4096];
    char *got = fgets(first_line, sizeof(first_line), f);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_CONTAINS(first_line, "message 0");
    fclose(f);

    unlink(tmpfile_path);
    unlink(outfile_path);
    return 0;
}

int traj_compress_file_with_budget(const char *input_path, const char *output_path,
                                   int target_tokens);

static int t_budget_aware_no_compress(void) {
    write_test_trajectory(60);
    int rc = traj_compress_file_with_budget(tmpfile_path, outfile_path, 999999);
    ASSERT_INT_EQ(rc, 0);
    unlink(tmpfile_path);
    return 0;
}

static int t_budget_aware_compress(void) {
    write_test_trajectory(60);
    int rc = traj_compress_file_with_budget(tmpfile_path, outfile_path, 100);
    ASSERT_GT(rc, 0);
    int lines = count_lines(outfile_path);
    ASSERT_GT(lines, 0);
    ASSERT(lines < 60);
    unlink(tmpfile_path);
    unlink(outfile_path);
    return 0;
}

int test_trajectory_compress_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("trajectory_compress");
    RUN_TEST(t_short_no_compress);
    RUN_TEST(t_long_compress);
    RUN_TEST(t_null_inputs);
    RUN_TEST(t_preserves_first);
    RUN_TEST(t_budget_aware_no_compress);
    RUN_TEST(t_budget_aware_compress);
    TEST_RETURN_FAILED();
}
