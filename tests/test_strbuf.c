#include "test.h"
#include "nevercode.h"
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_init(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    ASSERT_NULL(sb.data);
    ASSERT_INT_EQ((int)sb.len, 0);
    ASSERT_INT_EQ((int)sb.cap, 0);
    return 0;
}

static int test_append(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append(&sb, "hello", 5);
    ASSERT_NOT_NULL(sb.data);
    ASSERT_INT_EQ((int)sb.len, 5);
    ASSERT_STR_EQ(sb.data, "hello");
    strbuf_free(&sb);
    return 0;
}

static int test_append_str(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "hello ");
    strbuf_append_str(&sb, "world");
    ASSERT_STR_EQ(sb.data, "hello world");
    ASSERT_INT_EQ((int)sb.len, 11);
    strbuf_free(&sb);
    return 0;
}

static int test_append_null(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, NULL);
    ASSERT_NULL(sb.data);
    ASSERT_INT_EQ((int)sb.len, 0);
    strbuf_free(&sb);
    return 0;
}

static int test_append_empty(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append(&sb, "x", 0);
    ASSERT_NULL(sb.data);
    ASSERT_INT_EQ((int)sb.len, 0);
    strbuf_free(&sb);
    return 0;
}

static int test_clear(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "test data");
    ASSERT_INT_EQ((int)sb.len, 9);
    strbuf_clear(&sb);
    ASSERT_INT_EQ((int)sb.len, 0);
    ASSERT_NOT_NULL(sb.data);
    ASSERT(sb.cap > 0);
    strbuf_free(&sb);
    return 0;
}

static int test_detach(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "detach me");
    char *ptr = strbuf_detach(&sb);
    ASSERT_NOT_NULL(ptr);
    ASSERT_STR_EQ(ptr, "detach me");
    ASSERT_NULL(sb.data);
    ASSERT_INT_EQ((int)sb.len, 0);
    free(ptr);
    return 0;
}

static int test_free(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "free me");
    strbuf_free(&sb);
    ASSERT_NULL(sb.data);
    ASSERT_INT_EQ((int)sb.len, 0);
    ASSERT_INT_EQ((int)sb.cap, 0);
    return 0;
}

static int test_large_append(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < 1000; i++)
        strbuf_append_str(&sb, "abcdefghij");
    ASSERT_INT_EQ((int)sb.len, 10000);
    ASSERT(sb.cap >= 10001);
    strbuf_free(&sb);
    return 0;
}

static int test_growth(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "a");
    size_t cap1 = sb.cap;
    ASSERT(cap1 >= 2);

    for (int i = 0; i < 300; i++)
        strbuf_append_str(&sb, "b");
    ASSERT(sb.cap > cap1);
    ASSERT_INT_EQ((int)sb.len, 301);
    strbuf_free(&sb);
    return 0;
}

static int test_read_file(void) {
    const char *tmp = "/tmp/_nc_test_strbuf_rf.txt";
    FILE *f = fopen(tmp, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "line1\nline2\n");
    fclose(f);

    size_t len = 0;
    char *data = strbuf_read_file(tmp, &len);
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ(data, "line1\nline2\n");
    ASSERT_INT_EQ((int)len, 12);
    free(data);
    remove(tmp);
    return 0;
}

static int test_read_file_missing(void) {
    char *data = strbuf_read_file("/tmp/_nc_nonexistent_4837291.txt", NULL);
    ASSERT_NULL(data);
    return 0;
}

static int test_read_file_empty(void) {
    const char *tmp = "/tmp/_nc_test_strbuf_empty.txt";
    FILE *f = fopen(tmp, "w");
    fclose(f);
    char *data = strbuf_read_file(tmp, NULL);
    ASSERT_NULL(data);
    remove(tmp);
    return 0;
}

static int test_read_file_text_empty(void) {
    const char *tmp = "/tmp/_nc_test_strbuf_text_empty.txt";
    FILE *f = fopen(tmp, "w");
    fclose(f);
    char *data = strbuf_read_file_text(tmp, NULL);
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ(data, "");
    free(data);
    remove(tmp);
    return 0;
}

static int test_read_file_max_ok(void) {
    const char *tmp = "/tmp/_nc_test_strbuf_max.txt";
    FILE *f = fopen(tmp, "w");
    ASSERT_NOT_NULL(f);
    fputs("abc", f);
    fclose(f);
    size_t n = 0;
    char *data = strbuf_read_file_max(tmp, &n, 100);
    ASSERT_NOT_NULL(data);
    ASSERT_STR_EQ(data, "abc");
    ASSERT_INT_EQ((int)n, 3);
    free(data);
    remove(tmp);
    return 0;
}

static int test_read_file_max_oversized(void) {
    const char *tmp = "/tmp/_nc_test_strbuf_max_big.txt";
    FILE *f = fopen(tmp, "w");
    ASSERT_NOT_NULL(f);
    for (int i = 0; i < 200; i++)
        fputc('x', f);
    fclose(f);
    char *data = strbuf_read_file_max(tmp, NULL, 50);
    ASSERT_NULL(data);
    remove(tmp);
    return 0;
}

static int test_has_suffix(void) {
    ASSERT(str_has_suffix("file.json", ".json"));
    ASSERT(str_has_suffix("a.md", ".md"));
    ASSERT(!str_has_suffix("file.txt", ".json"));
    ASSERT(!str_has_suffix(".js", ".json"));
    ASSERT(!str_has_suffix("", ".json"));
    ASSERT(!str_has_suffix(NULL, ".json"));
    ASSERT(!str_has_suffix("file.json", NULL));
    ASSERT(str_has_suffix(".so", ".so"));
    ASSERT(str_has_suffix("plugin.dylib", ".dylib"));
    return 0;
}

static int test_nc_mkdir_p(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nc_test_mkdir_%d/a/b/c", getpid());
    ASSERT_INT_EQ(0, nc_mkdir_p(path));
    struct stat st;
    ASSERT_INT_EQ(0, stat(path, &st));
    ASSERT(S_ISDIR(st.st_mode));
    ASSERT_INT_EQ(0, nc_mkdir_p(path));
    rmdir(path);
    snprintf(path, sizeof(path), "/tmp/nc_test_mkdir_%d/a/b", getpid());
    rmdir(path);
    snprintf(path, sizeof(path), "/tmp/nc_test_mkdir_%d/a", getpid());
    rmdir(path);
    snprintf(path, sizeof(path), "/tmp/nc_test_mkdir_%d", getpid());
    rmdir(path);
    ASSERT_INT_EQ(-1, nc_mkdir_p(NULL));
    ASSERT_INT_EQ(-1, nc_mkdir_p(""));
    return 0;
}

static int test_nc_exec_quiet_success(void) {
    ASSERT_INT_EQ(0, nc_exec_quiet("true"));
    return 0;
}

static int test_nc_exec_quiet_failure(void) {
    ASSERT_INT_EQ(-1, nc_exec_quiet("false"));
    return 0;
}

static int test_nc_exec_quiet_null(void) {
    ASSERT_INT_EQ(-1, nc_exec_quiet(NULL));
    return 0;
}

static int test_nc_exec_quiet_output_discarded(void) {
    ASSERT_INT_EQ(0, nc_exec_quiet("echo hello >/dev/null"));
    return 0;
}

static int test_nc_cmd_exists_known(void) {
    ASSERT_TRUE(nc_cmd_exists("ls"));
    ASSERT_TRUE(nc_cmd_exists("sh"));
    return 0;
}

static int test_nc_cmd_exists_missing(void) {
    ASSERT_FALSE(nc_cmd_exists("__nonexistent_binary_42__"));
    return 0;
}

static int test_nc_cmd_exists_null(void) {
    ASSERT_FALSE(nc_cmd_exists(NULL));
    ASSERT_FALSE(nc_cmd_exists(""));
    return 0;
}

static int test_nc_cmd_exists_slash(void) {
    ASSERT_FALSE(nc_cmd_exists("/bin/ls"));
    return 0;
}

static int test_nc_open_devnull(void) {
    int fd_r = nc_open_devnull(O_RDONLY);
    ASSERT_TRUE(fd_r >= 0);
    char buf[4];
    ASSERT_INT_EQ((int)read(fd_r, buf, sizeof(buf)), 0);
    close(fd_r);

    int fd_w = nc_open_devnull(O_WRONLY);
    ASSERT_TRUE(fd_w >= 0);
    ASSERT_TRUE(write(fd_w, "x", 1) == 1);
    close(fd_w);

    int fd_rw = nc_open_devnull(O_RDWR);
    ASSERT_TRUE(fd_rw >= 0);
    close(fd_rw);
    return 0;
}

static int test_nc_parse_bool(void) {
    ASSERT_TRUE(nc_parse_bool("true", false));
    ASSERT_TRUE(nc_parse_bool("True", false));
    ASSERT_TRUE(nc_parse_bool("TRUE", false));
    ASSERT_TRUE(nc_parse_bool("1", false));
    ASSERT_TRUE(nc_parse_bool("yes", false));
    ASSERT_TRUE(nc_parse_bool("YES", false));
    ASSERT_TRUE(nc_parse_bool("on", false));
    ASSERT_TRUE(nc_parse_bool("ON", false));

    ASSERT_FALSE(nc_parse_bool("false", true));
    ASSERT_FALSE(nc_parse_bool("False", true));
    ASSERT_FALSE(nc_parse_bool("0", true));
    ASSERT_FALSE(nc_parse_bool("no", true));
    ASSERT_FALSE(nc_parse_bool("off", true));
    ASSERT_FALSE(nc_parse_bool("OFF", true));

    ASSERT_TRUE(nc_parse_bool(NULL, true));
    ASSERT_FALSE(nc_parse_bool(NULL, false));
    ASSERT_TRUE(nc_parse_bool("", true));
    ASSERT_FALSE(nc_parse_bool("", false));
    ASSERT_TRUE(nc_parse_bool("garbage", true));
    ASSERT_FALSE(nc_parse_bool("garbage", false));
    return 0;
}

static int test_nc_get_proxy_env_none(void) {
    unsetenv("HTTPS_PROXY");
    unsetenv("https_proxy");
    unsetenv("HTTP_PROXY");
    unsetenv("http_proxy");
    unsetenv("ALL_PROXY");
    unsetenv("all_proxy");
    ASSERT_NULL(nc_get_proxy_env());
    return 0;
}

static int test_nc_get_proxy_env_https(void) {
    unsetenv("HTTPS_PROXY");
    unsetenv("https_proxy");
    unsetenv("HTTP_PROXY");
    unsetenv("http_proxy");
    unsetenv("ALL_PROXY");
    unsetenv("all_proxy");
    setenv("HTTPS_PROXY", "http://proxy:8080", 1);
    const char *r = nc_get_proxy_env();
    ASSERT(r != NULL);
    ASSERT_STR_EQ(r, "http://proxy:8080");
    unsetenv("HTTPS_PROXY");
    return 0;
}

static int test_nc_get_proxy_env_fallback(void) {
    unsetenv("HTTPS_PROXY");
    unsetenv("https_proxy");
    unsetenv("HTTP_PROXY");
    unsetenv("http_proxy");
    unsetenv("ALL_PROXY");
    unsetenv("all_proxy");
    setenv("all_proxy", "socks5://p:1080", 1);
    const char *r = nc_get_proxy_env();
    ASSERT(r != NULL);
    ASSERT_STR_EQ(r, "socks5://p:1080");
    unsetenv("all_proxy");
    return 0;
}

static int test_nc_get_proxy_env_empty_skipped(void) {
    unsetenv("HTTPS_PROXY");
    unsetenv("https_proxy");
    unsetenv("HTTP_PROXY");
    unsetenv("http_proxy");
    unsetenv("ALL_PROXY");
    unsetenv("all_proxy");
    setenv("HTTPS_PROXY", "", 1);
    setenv("HTTP_PROXY", "http://real:3128", 1);
    const char *r = nc_get_proxy_env();
    ASSERT(r != NULL);
    ASSERT_STR_EQ(r, "http://real:3128");
    unsetenv("HTTPS_PROXY");
    unsetenv("HTTP_PROXY");
    return 0;
}

static int test_buf_macro_hierarchy(void) {
    ASSERT(NC_NUM_MAX < NC_SHORT_MAX);
    ASSERT(NC_SHORT_MAX < NC_FMT_MAX);
    ASSERT(NC_FMT_MAX < NC_PREVIEW_MAX);
    ASSERT(NC_PREVIEW_MAX < NC_TAG_MAX);
    ASSERT(NC_TAG_MAX < NC_LABEL_MAX);
    ASSERT(NC_LABEL_MAX < NC_MSG_MAX);
    ASSERT(NC_MSG_MAX < NC_LINE_MAX);
    ASSERT(NC_LINE_MAX < NC_PATH_MAX);
    return 0;
}

static int test_backoff_exp2_sat(void) {
    ASSERT_INT_EQ(0, nc_backoff_exp2_sat(0, 1000, 3));
    ASSERT_INT_EQ(0, nc_backoff_exp2_sat(100, 0, 3));
    ASSERT_INT_EQ(100, nc_backoff_exp2_sat(100, 1000, -5));
    ASSERT_INT_EQ(100, nc_backoff_exp2_sat(100, 1000, 0));
    ASSERT_INT_EQ(200, nc_backoff_exp2_sat(100, 1000, 1));
    ASSERT_INT_EQ(800, nc_backoff_exp2_sat(100, 1000, 3));
    ASSERT_INT_EQ(1000, nc_backoff_exp2_sat(100, 1000, 20));
    ASSERT_INT_EQ(1000, nc_backoff_exp2_sat(1000, 100, 3));
    return 0;
}

static int test_nc_home_dir(void) {
    const char *h = nc_home_dir();
    ASSERT(h != NULL);
    ASSERT(h[0] == '/');
    return 0;
}

static int test_nc_home_dir_cached(void) {
    const char *a = nc_home_dir();
    const char *b = nc_home_dir();
    ASSERT(a == b);
    return 0;
}

static int test_nc_user(void) {
    nc_env_reset();
    setenv("USER", "testrunner", 1);
    nc_env_reset();
    const char *u = nc_user();
    ASSERT(u != NULL);
    ASSERT(strcmp(u, "testrunner") == 0);
    const char *u2 = nc_user();
    ASSERT(u == u2);
    nc_env_reset();
    return 0;
}

static int test_nc_shell_env(void) {
    nc_env_reset();
    setenv("SHELL", "/bin/zsh", 1);
    nc_env_reset();
    const char *s = nc_shell_env();
    ASSERT(s != NULL);
    ASSERT(strstr(s, "zsh") != NULL || strstr(s, "bash") != NULL || s[0] == '/');
    nc_env_reset();
    return 0;
}

static int test_nc_tmpdir_fallback(void) {
    nc_env_reset();
    unsetenv("TMPDIR");
    nc_env_reset();
    const char *t = nc_tmpdir();
    ASSERT(t != NULL);
    ASSERT(strcmp(t, "/tmp") == 0);
    nc_env_reset();
    return 0;
}

static int test_nc_tmpdir_env(void) {
    nc_env_reset();
    setenv("TMPDIR", "/var/folders/test", 1);
    nc_env_reset();
    const char *t = nc_tmpdir();
    ASSERT(t != NULL);
    ASSERT(strcmp(t, "/var/folders/test") == 0);
    unsetenv("TMPDIR");
    nc_env_reset();
    return 0;
}

static int test_nc_term_env(void) {
    nc_env_reset();
    setenv("TERM", "xterm-256color", 1);
    nc_env_reset();
    const char *t = nc_term_env();
    ASSERT(t != NULL);
    ASSERT(strcmp(t, "xterm-256color") == 0);
    nc_env_reset();
    return 0;
}

static int test_nc_env_reset(void) {
    setenv("USER", "env_reset_val1", 1);
    nc_env_reset();
    const char *a = nc_user();
    ASSERT(a != NULL);
    ASSERT(strcmp(a, "env_reset_val1") == 0);

    setenv("USER", "env_reset_val2", 1);
    nc_env_reset();
    const char *b = nc_user();
    ASSERT(b != NULL);
    ASSERT(strcmp(b, "env_reset_val2") == 0);

    nc_env_reset();
    return 0;
}

static int test_nc_get_cwd(void) {
    char buf[NC_PATH_MAX];
    ASSERT(nc_get_cwd(buf, sizeof(buf)));
    ASSERT(buf[0] == '/');
    ASSERT(strlen(buf) > 0);
    return 0;
}

static int test_nc_get_cwd_null(void) {
    ASSERT(!nc_get_cwd(NULL, NC_PATH_MAX));
    char buf[4];
    ASSERT(!nc_get_cwd(buf, 0));
    return 0;
}

static int test_nc_safe_atol(void) {
    ASSERT_INT_EQ(0, (int)nc_safe_atol(NULL, 0));
    ASSERT_INT_EQ(42, (int)nc_safe_atol("", 42));
    ASSERT_INT_EQ(99, (int)nc_safe_atol("abc", 99));
    ASSERT_INT_EQ(123, (int)nc_safe_atol("123", 0));
    ASSERT_INT_EQ(-5, (int)nc_safe_atol("-5", 0));
    return 0;
}

static int test_nc_safe_atol_hex(void) {
    ASSERT_INT_EQ(0, (int)nc_safe_atol_hex(NULL, 0));
    ASSERT_INT_EQ(7, (int)nc_safe_atol_hex("", 7));
    ASSERT_INT_EQ(255, (int)nc_safe_atol_hex("ff", 0));
    ASSERT_INT_EQ(0, (int)nc_safe_atol_hex("0", 99));
    ASSERT_INT_EQ(16, (int)nc_safe_atol_hex("10", 0));
    ASSERT_INT_EQ(88, (int)nc_safe_atol_hex("xyz", 88));
    return 0;
}

static int test_nc_strlcpy_null_safe(void) {
    char buf[16] = "dirty";
    ASSERT_INT_EQ(0, (int)nc_strlcpy(buf, NULL, sizeof(buf)));
    ASSERT_STR_EQ(buf, "");
    ASSERT_INT_EQ(0, (int)nc_strlcpy(buf, "hello", 0));
    nc_strlcpy(buf, "abc", sizeof(buf));
    ASSERT_STR_EQ(buf, "abc");
    nc_strlcpy(buf, "toolongstringdata", 4);
    ASSERT_STR_EQ(buf, "too");
    return 0;
}

static int test_nc_safe_atof(void) {
    ASSERT(nc_safe_atof(NULL, 1.5) == 1.5);
    ASSERT(nc_safe_atof("", 2.0) == 2.0);
    ASSERT(nc_safe_atof("abc", 3.0) == 3.0);
    ASSERT(nc_safe_atof("1.5", 0.0) == 1.5);
    ASSERT(nc_safe_atof("-0.25", 0.0) == -0.25);
    ASSERT(nc_safe_atof("0", 99.0) == 0.0);
    return 0;
}

static int test_nc_contains_ci(void) {
    ASSERT(nc_contains_ci("Hello World", "world"));
    ASSERT(nc_contains_ci("FOOBAR", "oob"));
    ASSERT(!nc_contains_ci("hello", "xyz"));
    ASSERT(!nc_contains_ci(NULL, "test"));
    ASSERT(!nc_contains_ci("test", NULL));
    ASSERT(nc_contains_ci("abc", ""));
    return 0;
}

static int test_nc_starts_with(void) {
    ASSERT(nc_starts_with("Hello World", "Hello"));
    ASSERT(nc_starts_with("/usr/local", "/usr"));
    ASSERT(!nc_starts_with("Hello World", "hello"));
    ASSERT(!nc_starts_with("hi", "hello"));
    ASSERT(!nc_starts_with(NULL, "test"));
    ASSERT(!nc_starts_with("test", NULL));
    ASSERT(nc_starts_with("abc", ""));
    return 0;
}

static int test_nc_starts_with_ci(void) {
    ASSERT(nc_starts_with_ci("Hello World", "hello"));
    ASSERT(nc_starts_with_ci("FOOBAR", "FOO"));
    ASSERT(!nc_starts_with_ci("hello", "world"));
    ASSERT(!nc_starts_with_ci(NULL, "test"));
    ASSERT(!nc_starts_with_ci("test", NULL));
    ASSERT(nc_starts_with_ci("abc", ""));
    ASSERT(!nc_starts_with_ci("hi", "hello"));
    return 0;
}

static int test_nc_ends_with_ci(void) {
    ASSERT(nc_ends_with_ci("Hello World", "WORLD"));
    ASSERT(nc_ends_with_ci("foobar.TXT", ".txt"));
    ASSERT(!nc_ends_with_ci("hello", "xyz"));
    ASSERT(!nc_ends_with_ci(NULL, "test"));
    ASSERT(!nc_ends_with_ci("test", NULL));
    ASSERT(nc_ends_with_ci("abc", ""));
    ASSERT(!nc_ends_with_ci("hi", "hello"));
    return 0;
}

static int test_nc_mkdir_p_for_file(void) {
    char dir[NC_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/nc_test_mkdirs_XXXXXX", nc_tmpdir());
    ASSERT(mkdtemp(dir) != NULL);
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/sub/deep/file.txt", dir);
    nc_mkdir_p_for_file(path);
    char parent[NC_PATH_MAX];
    snprintf(parent, sizeof(parent), "%s/sub/deep", dir);
    ASSERT(nc_dir_exists(parent));
    nc_mkdir_p_for_file(NULL);
    nc_rm_rf(dir);
    return 0;
}

static int test_nc_rm_rf(void) {
    char dir[NC_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/nc_test_rmrf_XXXXXX", nc_tmpdir());
    ASSERT(mkdtemp(dir) != NULL);

    char sub[NC_PATH_MAX];
    snprintf(sub, sizeof(sub), "%s/a/b", dir);
    nc_mkdir_p(sub);

    char file1[NC_PATH_MAX];
    snprintf(file1, sizeof(file1), "%s/a/b/f.txt", dir);
    FILE *fp = fopen(file1, "w");
    ASSERT(fp != NULL);
    fputs("data", fp);
    fclose(fp);

    char file2[NC_PATH_MAX];
    snprintf(file2, sizeof(file2), "%s/a/top.txt", dir);
    fp = fopen(file2, "w");
    ASSERT(fp != NULL);
    fputs("top", fp);
    fclose(fp);

    ASSERT(nc_dir_exists(sub));
    ASSERT(nc_file_exists(file1));
    ASSERT_INT_EQ(0, nc_rm_rf(dir));
    ASSERT(!nc_dir_exists(dir));

    ASSERT_INT_EQ(0, nc_rm_rf(NULL));
    ASSERT_INT_EQ(0, nc_rm_rf("/tmp/nonexistent_nc_test_path_xyzzy"));
    return 0;
}

static int test_nc_trunc_preview(void) {
    char buf[16];
    nc_trunc_preview(buf, "hello", sizeof(buf));
    ASSERT_STR_EQ(buf, "hello");
    nc_trunc_preview(buf, "abcdefghij12345678", sizeof(buf));
    ASSERT_INT_EQ((int)strlen(buf), 15);
    ASSERT_STR_EQ(buf + 12, "...");
    nc_trunc_preview(buf, NULL, sizeof(buf));
    ASSERT_STR_EQ(buf, "");
    char tiny[3];
    nc_trunc_preview(tiny, "long string", sizeof(tiny));
    ASSERT_STR_EQ(tiny, "");
    char exact[6];
    nc_trunc_preview(exact, "abcde", sizeof(exact));
    ASSERT_STR_EQ(exact, "abcde");
    return 0;
}

static int test_nc_wall_sec(void) {
    long t1 = nc_wall_sec();
    ASSERT(t1 > 0);
    long t2 = nc_wall_sec();
    ASSERT(t2 >= t1);
    ASSERT(t2 - t1 <= 1);
    return 0;
}

static int test_nc_trim(void) {
    ASSERT(nc_trim(NULL) == NULL);

    char empty[] = "   ";
    ASSERT_STR_EQ(nc_trim(empty), "");

    char normal[] = "  hello world  ";
    ASSERT_STR_EQ(nc_trim(normal), "hello world");

    char no_ws[] = "abc";
    ASSERT_STR_EQ(nc_trim(no_ws), "abc");

    char tabs[] = "\t\n mixed \r\n";
    ASSERT_STR_EQ(nc_trim(tabs), "mixed");
    return 0;
}

static int test_nc_cwd_join(void) {
    char buf[NC_PATH_MAX];
    nc_cwd_join(buf, sizeof(buf), "test.dat");
    ASSERT(strlen(buf) > strlen("test.dat"));
    ASSERT(strstr(buf, "/test.dat") != NULL);
    return 0;
}

static int test_nc_fmt_tokens(void) {
    char buf[NC_FMT_MAX];
    nc_fmt_tokens(buf, sizeof(buf), 42);
    ASSERT_STR_EQ(buf, "42");

    nc_fmt_tokens(buf, sizeof(buf), 0);
    ASSERT_STR_EQ(buf, "0");

    nc_fmt_tokens(buf, sizeof(buf), 999);
    ASSERT_STR_EQ(buf, "999");

    nc_fmt_tokens(buf, sizeof(buf), 1500);
    ASSERT_STR_EQ(buf, "1.5k");

    nc_fmt_tokens(buf, sizeof(buf), 2500000);
    ASSERT_STR_EQ(buf, "2.50M");

    nc_fmt_tokens(buf, sizeof(buf), 100000000L);
    ASSERT_STR_EQ(buf, "100.00M");

    nc_fmt_tokens(buf, sizeof(buf), (long)INT_MAX + 1);
    ASSERT(strstr(buf, "M") != NULL || strstr(buf, "k") != NULL);
    return 0;
}

static int test_nc_fmt_duration_ms(void) {
    char buf[NC_FMT_MAX];
    nc_fmt_duration_ms(buf, sizeof(buf), 500);
    ASSERT_STR_EQ(buf, "500ms");

    nc_fmt_duration_ms(buf, sizeof(buf), 0);
    ASSERT_STR_EQ(buf, "0ms");

    nc_fmt_duration_ms(buf, sizeof(buf), 3500);
    ASSERT_STR_EQ(buf, "3.5s");

    nc_fmt_duration_ms(buf, sizeof(buf), 90000);
    ASSERT_STR_EQ(buf, "1m30s");

    nc_fmt_duration_ms(buf, sizeof(buf), 150000);
    ASSERT_STR_EQ(buf, "2m30s");
    return 0;
}

static int test_nc_fmt_elapsed_s(void) {
    char buf[NC_FMT_MAX];
    nc_fmt_elapsed_s(buf, sizeof(buf), 0);
    ASSERT_STR_EQ(buf, "0s");

    nc_fmt_elapsed_s(buf, sizeof(buf), 30);
    ASSERT_STR_EQ(buf, "30s");

    nc_fmt_elapsed_s(buf, sizeof(buf), 59);
    ASSERT_STR_EQ(buf, "59s");

    nc_fmt_elapsed_s(buf, sizeof(buf), 60);
    ASSERT_STR_EQ(buf, "1m00s");

    nc_fmt_elapsed_s(buf, sizeof(buf), 125);
    ASSERT_STR_EQ(buf, "2m05s");

    nc_fmt_elapsed_s(buf, sizeof(buf), 3600);
    ASSERT_STR_EQ(buf, "1h00m");

    nc_fmt_elapsed_s(buf, sizeof(buf), 3700);
    ASSERT_STR_EQ(buf, "1h01m");

    nc_fmt_elapsed_s(buf, sizeof(buf), 7325);
    ASSERT_STR_EQ(buf, "2h02m");
    return 0;
}

static int test_nc_min_max(void) {
    ASSERT_INT_EQ(NC_MIN(3, 5), 3);
    ASSERT_INT_EQ(NC_MIN(5, 3), 3);
    ASSERT_INT_EQ(NC_MIN(-1, 0), -1);
    ASSERT_INT_EQ(NC_MAX(3, 5), 5);
    ASSERT_INT_EQ(NC_MAX(5, 3), 5);
    ASSERT_INT_EQ(NC_MAX(-1, 0), 0);
    ASSERT_INT_EQ(NC_MIN(42, 42), 42);
    ASSERT_INT_EQ(NC_MAX(42, 42), 42);
    return 0;
}

static int test_nc_rand_uniform(void) {
    for (int i = 0; i < 200; i++) {
        uint32_t v = nc_rand_uniform(10);
        ASSERT(v < 10);
    }
    ASSERT_INT_EQ((int)nc_rand_uniform(0), 0);
    ASSERT_INT_EQ((int)nc_rand_uniform(1), 0);
    uint32_t big = nc_rand_uniform(UINT32_MAX);
    ASSERT(big < UINT32_MAX);
    return 0;
}

static int test_nc_rand_double(void) {
    for (int i = 0; i < 200; i++) {
        double d = nc_rand_double();
        ASSERT(d >= 0.0 && d <= 1.0);
    }
    return 0;
}

static int test_nc_rand_u32_nonzero_variance(void) {
    uint32_t first = nc_rand_u32();
    int seen_diff = 0;
    for (int i = 0; i < 50; i++) {
        if (nc_rand_u32() != first) {
            seen_diff = 1;
            break;
        }
    }
    ASSERT(seen_diff);
    return 0;
}

static int test_nc_time_now(void) {
    time_t t1 = nc_time_now();
    ASSERT(t1 > 0);
    time_t t2 = nc_time_now();
    ASSERT(t2 >= t1);
    return 0;
}

static int test_nc_cjson_str(void) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", "hello");
    cJSON_AddNumberToObject(obj, "num", 42);
    ASSERT(strcmp(nc_cjson_str(obj, "name", ""), "hello") == 0);
    ASSERT(strcmp(nc_cjson_str(obj, "missing", "fb"), "fb") == 0);
    ASSERT(nc_cjson_str(obj, "missing", NULL) == NULL);
    ASSERT(strcmp(nc_cjson_str(obj, "num", "x"), "x") == 0);
    cJSON_Delete(obj);
    return 0;
}

static int test_nc_cjson_int(void) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "count", 7);
    cJSON_AddStringToObject(obj, "text", "hi");
    ASSERT(nc_cjson_int(obj, "count", -1) == 7);
    ASSERT(nc_cjson_int(obj, "missing", 99) == 99);
    ASSERT(nc_cjson_int(obj, "text", -1) == -1);
    cJSON_Delete(obj);
    return 0;
}

static int test_nc_cjson_dbl(void) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "rate", 3.14);
    ASSERT(nc_cjson_dbl(obj, "rate", 0.0) > 3.13);
    ASSERT(nc_cjson_dbl(obj, "rate", 0.0) < 3.15);
    ASSERT(nc_cjson_dbl(obj, "missing", -1.0) == -1.0);
    cJSON_Delete(obj);
    return 0;
}

static int test_nc_cjson_bool(void) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddTrueToObject(obj, "on");
    cJSON_AddFalseToObject(obj, "off");
    cJSON_AddStringToObject(obj, "text", "yes");
    ASSERT(nc_cjson_bool(obj, "on", false) == true);
    ASSERT(nc_cjson_bool(obj, "off", true) == false);
    ASSERT(nc_cjson_bool(obj, "missing", true) == true);
    ASSERT(nc_cjson_bool(obj, "missing", false) == false);
    ASSERT(nc_cjson_bool(obj, "text", false) == false);
    cJSON_Delete(obj);
    return 0;
}

static int test_nc_cjson_detach(void) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "k", "v");
    char *s = nc_cjson_detach(obj);
    ASSERT(s != NULL);
    ASSERT(strstr(s, "\"k\"") != NULL);
    ASSERT(strstr(s, "\"v\"") != NULL);
    free(s);

    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(1));
    char *pretty = nc_cjson_detach_pretty(arr);
    ASSERT(pretty != NULL);
    ASSERT(strstr(pretty, "1") != NULL);
    free(pretty);
    return 0;
}

static int test_nc_cjson_dup_str(void) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", "world");
    char *v = nc_cjson_dup_str(obj, "name", "fb");
    ASSERT(v != NULL);
    ASSERT(strcmp(v, "world") == 0);
    free(v);
    char *fb = nc_cjson_dup_str(obj, "missing", "default");
    ASSERT(fb != NULL);
    ASSERT(strcmp(fb, "default") == 0);
    free(fb);
    char *n = nc_cjson_dup_str(obj, "missing", NULL);
    ASSERT(n == NULL);
    cJSON_Delete(obj);
    return 0;
}

int test_strbuf_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("strbuf");
    RUN_TEST(test_init);
    RUN_TEST(test_append);
    RUN_TEST(test_append_str);
    RUN_TEST(test_append_null);
    RUN_TEST(test_append_empty);
    RUN_TEST(test_clear);
    RUN_TEST(test_detach);
    RUN_TEST(test_free);
    RUN_TEST(test_large_append);
    RUN_TEST(test_growth);
    RUN_TEST(test_read_file);
    RUN_TEST(test_read_file_missing);
    RUN_TEST(test_read_file_empty);
    RUN_TEST(test_read_file_text_empty);
    RUN_TEST(test_read_file_max_ok);
    RUN_TEST(test_read_file_max_oversized);
    RUN_TEST(test_has_suffix);
    RUN_TEST(test_nc_mkdir_p);
    RUN_TEST(test_nc_exec_quiet_success);
    RUN_TEST(test_nc_exec_quiet_failure);
    RUN_TEST(test_nc_exec_quiet_null);
    RUN_TEST(test_nc_exec_quiet_output_discarded);
    RUN_TEST(test_nc_cmd_exists_known);
    RUN_TEST(test_nc_cmd_exists_missing);
    RUN_TEST(test_nc_cmd_exists_null);
    RUN_TEST(test_nc_cmd_exists_slash);
    RUN_TEST(test_nc_open_devnull);
    RUN_TEST(test_nc_parse_bool);
    RUN_TEST(test_nc_get_proxy_env_none);
    RUN_TEST(test_nc_get_proxy_env_https);
    RUN_TEST(test_nc_get_proxy_env_fallback);
    RUN_TEST(test_nc_get_proxy_env_empty_skipped);
    RUN_TEST(test_buf_macro_hierarchy);
    RUN_TEST(test_backoff_exp2_sat);
    RUN_TEST(test_nc_home_dir);
    RUN_TEST(test_nc_home_dir_cached);
    RUN_TEST(test_nc_user);
    RUN_TEST(test_nc_shell_env);
    RUN_TEST(test_nc_tmpdir_fallback);
    RUN_TEST(test_nc_tmpdir_env);
    RUN_TEST(test_nc_term_env);
    RUN_TEST(test_nc_env_reset);
    RUN_TEST(test_nc_get_cwd);
    RUN_TEST(test_nc_get_cwd_null);
    RUN_TEST(test_nc_strlcpy_null_safe);
    RUN_TEST(test_nc_safe_atol);
    RUN_TEST(test_nc_safe_atol_hex);
    RUN_TEST(test_nc_safe_atof);
    RUN_TEST(test_nc_contains_ci);
    RUN_TEST(test_nc_starts_with);
    RUN_TEST(test_nc_starts_with_ci);
    RUN_TEST(test_nc_ends_with_ci);
    RUN_TEST(test_nc_mkdir_p_for_file);
    RUN_TEST(test_nc_rm_rf);
    RUN_TEST(test_nc_trunc_preview);
    RUN_TEST(test_nc_wall_sec);
    RUN_TEST(test_nc_trim);
    RUN_TEST(test_nc_cwd_join);
    RUN_TEST(test_nc_fmt_tokens);
    RUN_TEST(test_nc_fmt_duration_ms);
    RUN_TEST(test_nc_fmt_elapsed_s);
    RUN_TEST(test_nc_min_max);
    RUN_TEST(test_nc_rand_uniform);
    RUN_TEST(test_nc_rand_double);
    RUN_TEST(test_nc_rand_u32_nonzero_variance);
    RUN_TEST(test_nc_time_now);
    RUN_TEST(test_nc_cjson_str);
    RUN_TEST(test_nc_cjson_int);
    RUN_TEST(test_nc_cjson_dbl);
    RUN_TEST(test_nc_cjson_bool);
    RUN_TEST(test_nc_cjson_detach);
    RUN_TEST(test_nc_cjson_dup_str);
    TEST_RETURN_FAILED();
}
