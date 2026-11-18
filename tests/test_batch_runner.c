#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <unistd.h>

static const char *write_temp_jsonl(const char *content) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_batch_%d.jsonl", getpid());
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
    return path;
}

static const char *summary_output_dir(char *buf, size_t cap) {
    char *s = batch_format_summary();
    if (!s)
        return NULL;
    const char *k = "  Output: ";
    char *p = strstr(s, k);
    if (!p) {
        free(s);
        return NULL;
    }
    p += strlen(k);
    char *eol = strchr(p, '\n');
    size_t n = eol ? (size_t)(eol - p) : strlen(p);
    if (n + 1 > cap)
        n = cap - 1;
    memcpy(buf, p, n);
    buf[n] = '\0';
    free(s);
    return buf;
}

static int t_load_jsonl(void) {
    const char *p = write_temp_jsonl("{\"prompt\":\"hello\",\"id\":\"p1\"}\n"
                                     "{\"prompt\":\"world\",\"id\":\"p2\"}\n");
    int n = batch_load_prompts(p);
    ASSERT_INT_EQ(n, 2);
    ASSERT_INT_EQ(batch_count(), 2);
    ASSERT_STR_EQ(batch_prompt_at(0), "hello");
    ASSERT_STR_EQ(batch_prompt_at(1), "world");
    ASSERT_STR_EQ(batch_id_at(0), "p1");
    ASSERT_STR_EQ(batch_id_at(1), "p2");
    batch_free();
    unlink(p);
    return 0;
}

static int t_load_plain_lines(void) {
    const char *p = write_temp_jsonl("line one\nline two\nline three\n");
    int n = batch_load_prompts(p);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(batch_prompt_at(0), "line one");
    ASSERT_STR_EQ(batch_prompt_at(2), "line three");
    batch_free();
    unlink(p);
    return 0;
}

static int t_counters(void) {
    const char *p = write_temp_jsonl("{\"prompt\":\"a\"}\n{\"prompt\":\"b\"}\n");
    batch_load_prompts(p);
    ASSERT_INT_EQ(batch_completed(), 0);
    ASSERT_INT_EQ(batch_failed(), 0);
    batch_record_result(0, "ok", true);
    ASSERT_INT_EQ(batch_completed(), 1);
    batch_record_result(1, "err", false);
    ASSERT_INT_EQ(batch_failed(), 1);
    batch_free();
    unlink(p);
    return 0;
}

static int t_format_summary(void) {
    const char *p = write_temp_jsonl("{\"prompt\":\"test\"}\n");
    batch_load_prompts(p);
    batch_record_result(0, "done", true);
    char *s = batch_format_summary();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, BATCH_RUN_ID_PREFIX "_");
    ASSERT_STR_CONTAINS(s, "Done: 1");
    free(s);
    batch_free();
    unlink(p);
    return 0;
}

static int t_capacity_macros(void) {
    ASSERT(BATCH_INIT_CAPACITY > 0);
    ASSERT(BATCH_MAX_PROMPTS >= BATCH_INIT_CAPACITY);
    ASSERT(strlen(BATCH_RUN_ID_PREFIX) > 0);
    return 0;
}

static int t_empty_file(void) {
    const char *p = write_temp_jsonl("");
    int n = batch_load_prompts(p);
    ASSERT_INT_EQ(n, 0);
    batch_free();
    unlink(p);
    return 0;
}

static int t_missing_file(void) {
    int n = batch_load_prompts("/tmp/does_not_exist_12345.jsonl");
    ASSERT_INT_EQ(n, -1);
    ASSERT_INT_EQ(batch_count(), 0);
    ASSERT_NULL(batch_prompt_at(0));
    batch_free();
    return 0;
}

static int t_sanitize_id_result_filename(void) {
    const char *p = write_temp_jsonl("{\"prompt\":\"hello\",\"id\":\"../../evil\"}\n");
    int n = batch_load_prompts(p);
    ASSERT_INT_EQ(n, 1);
    batch_record_result(0, "ok", true);

    char out_dir[NC_PATH_MAX];
    ASSERT_NOT_NULL(summary_output_dir(out_dir, sizeof(out_dir)));

    char safe_path[NC_PATH_MAX];
    snprintf(safe_path, sizeof(safe_path), "%.4077s/______evil.json", out_dir);
    ASSERT_INT_EQ(access(safe_path, F_OK), 0);

    batch_free();
    unlink(p);
    return 0;
}

static int t_checkpoint_clamp(void) {
    const char *p = write_temp_jsonl("{\"prompt\":\"a\"}\n{\"prompt\":\"b\"}\n");
    int n = batch_load_prompts(p);
    ASSERT_INT_EQ(n, 2);
    batch_save_checkpoint(9999);
    ASSERT_INT_EQ(batch_resume_index(), 2);
    batch_save_checkpoint(-8);
    ASSERT_INT_EQ(batch_resume_index(), 0);
    batch_free();
    unlink(p);
    return 0;
}

int test_batch_runner_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("batch_runner");
    RUN_TEST(t_load_jsonl);
    RUN_TEST(t_load_plain_lines);
    RUN_TEST(t_counters);
    RUN_TEST(t_format_summary);
    RUN_TEST(t_capacity_macros);
    RUN_TEST(t_empty_file);
    RUN_TEST(t_missing_file);
    RUN_TEST(t_sanitize_id_result_filename);
    RUN_TEST(t_checkpoint_clamp);
    TEST_RETURN_FAILED();
}
