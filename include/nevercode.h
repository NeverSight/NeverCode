#ifndef NEVERCODE_H
#define NEVERCODE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/random.h>
#endif
#include <time.h>
#include "cJSON.h"
#include "defaults.h"

typedef struct conversation_s conversation_t;

#define NEVERCODE_VERSION NEVERCODE_VERSION_STR

#define NC_ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define NC_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NC_MAX(a, b) ((a) > (b) ? (a) : (b))

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define NC_NORETURN _Noreturn
#elif defined(__GNUC__) || defined(__clang__)
#define NC_NORETURN __attribute__((noreturn))
#else
#define NC_NORETURN
#endif

/* Abort with a message on unrecoverable errors (OOM, invariant violation). */
NC_NORETURN static inline void nc_fatal(const char *msg) {
    fprintf(stderr, "fatal: %s\n", msg ? msg : "unknown");
    abort();
}

/* Safe string copy: always NUL-terminates, returns bytes written (excl. NUL).
 * Drop-in replacement for strncpy that doesn't leave unterminated buffers. */
static inline size_t nc_strlcpy(char *dst, const char *src, size_t sz) {
    if (!sz)
        return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    size_t i = 0;
    while (i < sz - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

/* Copy src into dst[cap], appending "..." when truncated.  Avoids the
 * repeated 5-line if/else memcpy pattern scattered across preview sites. */
static inline void nc_trunc_preview(char *dst, const char *src, size_t cap) {
    if (!cap)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t slen = strlen(src);
    if (slen < cap) {
        memcpy(dst, src, slen + 1);
    } else if (cap > 4) {
        memcpy(dst, src, cap - 4);
        memcpy(dst + cap - 4, "...", 4);
    } else {
        dst[0] = '\0';
    }
}

/* Safe boolean parsing: recognises true/1/yes/on and false/0/no/off (case-insensitive). */
static inline bool nc_parse_bool(const char *s, bool fallback) {
    if (!s || !*s)
        return fallback;
    if (s[0] == '1' && s[1] == '\0')
        return true;
    if (s[0] == '0' && s[1] == '\0')
        return false;
    if (strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 || strcasecmp(s, "on") == 0)
        return true;
    if (strcasecmp(s, "false") == 0 || strcasecmp(s, "no") == 0 || strcasecmp(s, "off") == 0)
        return false;
    return fallback;
}

/* OOM-safe strdup: returns NULL when src is NULL, aborts on allocation failure.
 * Drop-in replacement for strdup() in paths where OOM is unrecoverable. */
static inline char *nc_xstrdup(const char *s) {
    if (!s)
        return NULL;
    char *d = strdup(s);
    if (!d) {
        fprintf(stderr, "fatal: out of memory (strdup)\n");
        abort();
    }
    return d;
}

/* OOM-safe strndup: aborts on allocation failure. */
static inline char *nc_xstrndup(const char *s, size_t n) {
    if (!s)
        return NULL;
    size_t len = strlen(s);
    if (len > n)
        len = n;
    char *d = malloc(len + 1);
    if (!d) {
        fprintf(stderr, "fatal: out of memory (strndup)\n");
        abort();
    }
    memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

/* Safe integer conversion: returns fallback on NULL/empty/non-numeric input. */
static inline int nc_safe_atoi(const char *s, int fallback) {
    if (!s || !*s)
        return fallback;
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s)
        return fallback;
    if (v > INT32_MAX)
        return INT32_MAX;
    if (v < INT32_MIN)
        return INT32_MIN;
    return (int)v;
}

static inline long nc_safe_atol(const char *s, long fallback) {
    if (!s || !*s)
        return fallback;
    char *end;
    long v = strtol(s, &end, 10);
    return (end == s) ? fallback : v;
}

static inline long nc_safe_atol_hex(const char *s, long fallback) {
    if (!s || !*s)
        return fallback;
    char *end;
    long v = strtol(s, &end, 16);
    return (end == s) ? fallback : v;
}

static inline double nc_safe_atof(const char *s, double fallback) {
    if (!s || !*s)
        return fallback;
    char *end;
    double v = strtod(s, &end);
    return (end == s) ? fallback : v;
}

int nc_mkdir_p(const char *path); /* forward decl for nc_ensure_dir below */

/* Check whether a path exists (file, dir, or any inode). */
static inline bool nc_path_exists(const char *path) {
    struct stat _nc_st;
    return path && stat(path, &_nc_st) == 0;
}

/* Check whether a regular file exists. */
static inline bool nc_file_exists(const char *path) {
    struct stat _nc_st;
    return path && stat(path, &_nc_st) == 0 && S_ISREG(_nc_st.st_mode);
}

/* Check whether a directory exists. */
static inline bool nc_dir_exists(const char *path) {
    struct stat _nc_st;
    return path && stat(path, &_nc_st) == 0 && S_ISDIR(_nc_st.st_mode);
}

/* Ensure a directory exists (create recursively if missing). */
static inline void nc_ensure_dir(const char *path) {
    struct stat _nc_st;
    if (stat(path, &_nc_st) != 0)
        nc_mkdir_p(path);
}

/* Ensure parent directory of a file path exists (for file-write helpers). */
static inline void nc_mkdir_p_for_file(const char *path) {
    char *tmp = nc_xstrdup(path);
    if (tmp) {
        char *dir = dirname(tmp);
        if (dir && strlen(dir) > 1)
            nc_mkdir_p(dir);
        free(tmp);
    }
}

/* Recursively remove a file or directory tree.  Returns 0 on success. */
static inline int nc_rm_rf(const char *path) {
    if (!path)
        return 0;
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return unlink(path);
    DIR *d = opendir(path);
    if (!d)
        return -1;
    int rc = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char child[NC_PATH_MAX];
        snprintf(child, sizeof(child), "%.2045s/%.2045s", path, ent->d_name);
        if (nc_rm_rf(child) != 0)
            rc = -1;
    }
    closedir(d);
    if (rmdir(path) != 0)
        rc = -1;
    return rc;
}

/* Case-insensitive substring search (NULL-safe strcasestr wrapper). */
static inline bool nc_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle)
        return false;
    return strcasestr(haystack, needle) != NULL;
}

/* Outbound HTTP(S) safety guard (SSRF protection) — see src/utils/url_guard.c.
 * struct curl_sockaddr is forward-declared; curl-typed helper lives in
 * include/url_guard.h which includes <curl/curl.h>. */
struct curl_sockaddr;
bool nc_url_scheme_http_https(const char *url);
bool nc_ipv4_is_public(uint32_t be_addr);
bool nc_ipv6_is_public(const unsigned char s[16]);
bool nc_sockaddr_is_public(const struct curl_sockaddr *addr);

/* Skip leading whitespace, returning pointer to first non-whitespace char. */
static inline const char *nc_skip_ws(const char *s) {
    if (!s)
        return s;
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

/* In-place trim: skip leading whitespace + NUL-terminate trailing whitespace. */
static inline char *nc_trim(char *s) {
    if (!s)
        return s;
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';
    return s;
}

/* Build path from cwd: snprintf(out, cap, "%s/%s", cwd, filename). */
static inline void nc_cwd_join(char *out, size_t cap, const char *filename) {
    char cwd[NC_PATH_MAX];
    const char *dir = getcwd(cwd, sizeof(cwd));
    if (!dir)
        dir = ".";
    snprintf(out, cap, "%s/%s", dir, filename);
}

/* Format token count: "999" / "12.3k" / "1.50M". */
static inline void nc_fmt_tokens(char *buf, size_t sz, long n) {
    if (n < 1000)
        snprintf(buf, sz, "%ld", n);
    else if (n < 1000000)
        snprintf(buf, sz, "%.1fk", n / 1000.0);
    else
        snprintf(buf, sz, "%.2fM", n / 1000000.0);
}

/* Format millisecond duration: "300ms" / "1.5s" / "2m30s". */
static inline void nc_fmt_duration_ms(char *buf, size_t sz, long ms) {
    if (ms < 1000)
        snprintf(buf, sz, "%ldms", ms);
    else if (ms < 60000)
        snprintf(buf, sz, "%.1fs", ms / 1000.0);
    else
        snprintf(buf, sz, "%ldm%lds", ms / 60000, (ms % 60000) / 1000);
}

/* Format second-precision elapsed: "30s" / "2m30s" / "1h02m". */
static inline void nc_fmt_elapsed_s(char *buf, size_t sz, int sec) {
    if (sec < 60)
        snprintf(buf, sz, "%ds", sec);
    else if (sec < 3600)
        snprintf(buf, sz, "%dm%02ds", sec / 60, sec % 60);
    else
        snprintf(buf, sz, "%dh%02dm", sec / 3600, (sec % 3600) / 60);
}

/* Case-sensitive prefix match. */
static inline bool nc_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix)
        return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Case-insensitive prefix match. */
static inline bool nc_starts_with_ci(const char *s, const char *prefix) {
    if (!s || !prefix)
        return false;
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
}

/* Case-insensitive suffix match. */
static inline bool nc_ends_with_ci(const char *s, const char *suffix) {
    if (!s || !suffix)
        return false;
    size_t sl = strlen(s), xl = strlen(suffix);
    if (xl > sl)
        return false;
    return strncasecmp(s + sl - xl, suffix, xl) == 0;
}

/* Get current working directory into caller buffer. */
static inline bool nc_get_cwd(char *out, size_t cap) {
    return out && cap > 0 && getcwd(out, cap) != NULL;
}

/* Monotonic timespec helpers — avoids duplicate ts_diff_ms in every UI file. */
static inline int64_t nc_ts_diff_ms(const struct timespec *a, const struct timespec *b) {
    return (int64_t)(a->tv_sec - b->tv_sec) * 1000 + (int64_t)(a->tv_nsec - b->tv_nsec) / 1000000;
}
static inline int64_t nc_ts_diff_us(const struct timespec *a, const struct timespec *b) {
    return (int64_t)(a->tv_sec - b->tv_sec) * 1000000 + (int64_t)(a->tv_nsec - b->tv_nsec) / 1000;
}

/* Monotonic timestamp in milliseconds (fallback to CLOCK_REALTIME if unavailable). */
static inline long nc_mono_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        clock_gettime(CLOCK_REALTIME, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}
static inline long nc_mono_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

/* Wall-clock as time_t (centralizes time(NULL) for testability). */
static inline time_t nc_time_now(void) {
    return time(NULL);
}
/* Wall-clock seconds as long (trivial but avoids duplicate static wrappers). */
static inline long nc_wall_sec(void) {
    return (long)nc_time_now();
}

/* Hash seeds for lightweight string hashing. */
#define NC_DJB2_SEED 5381u
#define NC_FNV1A_OFFSET 2166136261u
#define NC_FNV1A_PRIME 16777619u

/* Saturating exponential backoff based on doubling.
 * attempt <= 0 returns base; values always clamp to [base, cap]. */
static inline int nc_backoff_exp2_sat(int base, int cap, int attempt) {
    if (base <= 0 || cap <= 0)
        return 0;
    if (base >= cap)
        return base;
    if (attempt <= 0)
        return base;

    int delay = base;
    while (attempt-- > 0 && delay < cap) {
        if (delay > cap / 2) {
            delay = cap;
            break;
        }
        delay *= 2;
    }
    if (delay > cap)
        delay = cap;
    return delay < base ? base : delay;
}

/* Thread-safe cryptographic random: no global state, no seeding required.
 * macOS uses arc4random; Linux uses getrandom(2) with /dev/urandom fallback. */
static inline uint32_t nc_rand_u32(void) {
#ifdef __APPLE__
    return arc4random();
#else
    uint32_t v = 0;
    if (getrandom(&v, sizeof(v), 0) == (ssize_t)sizeof(v))
        return v;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t _n = read(fd, &v, sizeof(v));
        if (_n != (ssize_t)sizeof(v))
            v = 0;
        close(fd);
        return v;
    }
    v = (uint32_t)nc_wall_sec() ^ (uint32_t)getpid();
    return v * 1103515245u + 12345u;
#endif
}
static inline uint32_t nc_rand_uniform(uint32_t upper) {
    if (upper <= 1)
        return 0;
#ifdef __APPLE__
    return arc4random_uniform(upper);
#else
    uint32_t min = ((uint32_t)(-(int32_t)upper)) % upper;
    for (;;) {
        uint32_t r = nc_rand_u32();
        if (r >= min)
            return r % upper;
    }
#endif
}
static inline double nc_rand_double(void) {
    return (double)nc_rand_u32() / (double)UINT32_MAX;
}

/* Post-fork child process hardening: detach session and request SIGTERM on parent exit (Linux). */
static inline void nc_child_setup(void) {
    setsid();
#ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
}

/* Create a pipe with FD_CLOEXEC on both ends. Returns 0 on success, -1 on error. */
static inline int nc_pipe_cloexec(int pipefd[2]) {
    if (pipe(pipefd) < 0)
        return -1;
    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    return 0;
}

/* Open /dev/null with the given flags (O_RDONLY / O_WRONLY / O_RDWR).
 * Returns fd on success, -1 on error. The fd has FD_CLOEXEC set. */
#define NC_DEV_NULL "/dev/null"
static inline int nc_open_devnull(int flags) {
    int fd = open(NC_DEV_NULL, flags);
    if (fd >= 0)
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

/* Set file descriptor to non-blocking mode. No-op for fd < 0. */
static inline void nc_set_nonblock(int fd) {
    if (fd < 0)
        return;
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0)
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* CJK fullwidth punctuation detection — returns byte length of the match, 0 if none. */
static inline size_t nc_is_cjk_punct(const char *s, size_t remaining) {
    static const char *const seqs[] = {
        "\xEF\xBC\x8C", "\xE3\x80\x82", "\xE3\x80\x81", "\xEF\xBC\x9B", "\xEF\xBC\x9A",
        "\xEF\xBC\x81", "\xEF\xBC\x9F", "\xEF\xBC\x89", "\xE3\x80\x91", "\xE3\x80\x8B",
    };
    if (remaining < 3)
        return 0;
    for (int i = 0; i < NC_ARRAY_LEN(seqs); i++)
        if (memcmp(s, seqs[i], 3) == 0)
            return 3;
    return 0;
}

/* Directories that are skipped during recursive tree walks (glob, listdir,
 * file index, repo map). Centralized here so all consumers share one list. */
static inline bool nc_is_noise_dir(const char *name) {
    static const char *const tbl[] = {
        "node_modules", "__pycache__",   ".git",     ".svn",        ".hg",    "build",
        "dist",         ".next",         ".nuxt",    ".output",     "target", "vendor",
        ".cache",       ".parcel-cache", "coverage", ".nyc_output", ".tox",   ".eggs",
        "venv",         ".venv",         "env",      NULL,
    };
    for (const char *const *p = tbl; *p; p++)
        if (strcmp(name, *p) == 0)
            return true;
    return false;
}

extern volatile sig_atomic_t g_interrupted;
/* Re-install SIGINT/SIGTERM handlers (readline/libedit may replace them). */
void nc_sigint_arm(void);

const char *config_home_dir(void);
void config_home_path(char *out, size_t sz, const char *subpath);
bool config_find_path(char *out, size_t sz, const char *subpath);
/* Clears cached config home (after HOME change or in tests). */
void config_home_reset_cache(void);

/* Hooks */
typedef enum {
    HOOK_PRE_TOOL_USE,
    HOOK_POST_TOOL_USE,
    HOOK_STOP,
    HOOK_PRE_COMPACT,
    HOOK_TEAMMATE_IDLE,
    HOOK_TASK_COMPLETED
} hook_event_t;

typedef enum { HOOK_DECISION_PROCEED, HOOK_DECISION_BLOCK, HOOK_DECISION_SKIP } hook_decision_t;

typedef struct {
    hook_event_t event;
    char *command;
    char *tool_pattern; /* glob pattern to match tool name, NULL = all */
    int timeout;
} hook_def_t;

typedef struct {
    hook_def_t hooks[MAX_HOOKS];
    int num_hooks;
} hook_ctx_t;

void hooks_init(hook_ctx_t *h);
void hooks_free(hook_ctx_t *h);
int hooks_load(hook_ctx_t *h, const char *path);
hook_decision_t hooks_run(hook_ctx_t *h, hook_event_t event, const char *tool_name,
                          const char *tool_input_json, char **additional_context);

typedef enum { PROVIDER_NATIVE, PROVIDER_COMPAT } provider_type_t;
bool nc_provider_parse(const char *value, provider_type_t *provider_out);
const char *nc_provider_slug(provider_type_t provider);
const char *nc_provider_label(provider_type_t provider);
const char *nc_provider_default_model(provider_type_t provider);

typedef enum { ROLE_SYSTEM, ROLE_USER, ROLE_ASSISTANT, ROLE_TOOL } message_role_t;

typedef enum { CONTENT_TEXT, CONTENT_TOOL_USE, CONTENT_TOOL_RESULT, CONTENT_IMAGE } content_type_t;

typedef struct {
    content_type_t type;
    char *text;
    char *tool_use_id;
    char *tool_name;
    cJSON *tool_input;
    int is_error;
    char *image_data;       /* base64-encoded image */
    char *image_media_type; /* e.g. "image/png" */
} content_block_t;

typedef struct {
    message_role_t role;
    content_block_t *blocks;
    int num_blocks;
    int cap_blocks;
} message_t;

typedef enum {
    STREAM_EVENT_TEXT_DELTA,
    STREAM_EVENT_TOOL_USE_BEGIN,
    STREAM_EVENT_TOOL_USE_DELTA,
    STREAM_EVENT_TOOL_USE_END,
    STREAM_EVENT_MESSAGE_STOP,
    STREAM_EVENT_USAGE,
    STREAM_EVENT_ERROR,
    STREAM_EVENT_THINKING_DELTA
} stream_event_type_t;

typedef struct {
    stream_event_type_t type;
    const char *text;
    const char *tool_use_id;
    const char *tool_name;
    const char *json_fragment;
    int stop_reason;
    const char *error_message;
    int input_tokens;
    int output_tokens;
    int cache_creation_tokens;
    int cache_read_tokens;
} stream_event_t;

typedef void (*stream_callback_t)(const stream_event_t *event, void *userdata);

typedef struct tool_def {
    const char *name;
    const char *description;
    cJSON *input_schema;
    int (*execute)(const cJSON *input, char **output, void *ctx);
    int (*check_permission)(const cJSON *input, void *ctx);
    bool read_only;
    bool concurrency_safe;
    bool destructive;
} tool_def_t;

#define STOP_END_TURN 0
#define STOP_TOOL_USE 1
#define STOP_MAX_TOKENS 2

/* Dynamic string buffer */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

void strbuf_init(strbuf_t *sb);
void strbuf_append(strbuf_t *sb, const char *s, size_t n);
void strbuf_append_str(strbuf_t *sb, const char *s);
void strbuf_appendf(strbuf_t *sb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void strbuf_clear(strbuf_t *sb);
void strbuf_free(strbuf_t *sb);
char *strbuf_detach(strbuf_t *sb);
char *strbuf_read_file(const char *path, size_t *out_len);
char *strbuf_read_file_max(const char *path, size_t *out_len, size_t max_bytes);
char *strbuf_read_file_text(const char *path, size_t *out_len);

/* Filesystem utilities */
int nc_mkdir_p(const char *path);
int nc_write_file_atomic(const char *path, const void *data, size_t len);

/* Write a NUL-terminated string to a file (non-atomic, truncates). */
static inline int nc_write_file_str(const char *path, const char *content) {
    FILE *_nc_f = fopen(path, "w");
    if (!_nc_f)
        return -1;
    int _nc_rc = 0;
    if (content && content[0])
        _nc_rc = (fputs(content, _nc_f) == EOF) ? -1 : 0;
    if (_nc_rc == 0 && fflush(_nc_f) != 0)
        _nc_rc = -1;
    fclose(_nc_f);
    return _nc_rc;
}

/* ── cJSON safe field accessors ── */

static inline const char *nc_cjson_str(const cJSON *obj, const char *key, const char *fallback) {
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsString(item)) ? item->valuestring : fallback;
}

static inline int nc_cjson_int(const cJSON *obj, const char *key, int fallback) {
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsNumber(item)) ? item->valueint : fallback;
}

static inline double nc_cjson_dbl(const cJSON *obj, const char *key, double fallback) {
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsNumber(item)) ? item->valuedouble : fallback;
}

static inline bool nc_cjson_bool(const cJSON *obj, const char *key, bool fallback) {
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item)
        return fallback;
    if (cJSON_IsTrue(item))
        return true;
    if (cJSON_IsFalse(item))
        return false;
    return fallback;
}

static inline char *nc_cjson_detach(cJSON *obj) {
    char *s = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return s;
}

static inline char *nc_cjson_detach_pretty(cJSON *obj) {
    char *s = cJSON_Print(obj);
    cJSON_Delete(obj);
    return s;
}

static inline char *nc_cjson_dup_str(const cJSON *obj, const char *key, const char *fallback) {
    const char *v = nc_cjson_str(obj, key, fallback);
    return v ? nc_xstrdup(v) : NULL;
}

/* JSON file convenience: load a file as cJSON (caller frees). */
static inline cJSON *nc_json_load(const char *path) {
    char *buf = strbuf_read_file(path, NULL);
    if (!buf)
        return NULL;
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    return j;
}

/* JSON file convenience: save a cJSON object to a file (non-atomic). */
static inline int nc_json_save(const char *path, const cJSON *obj) {
    char *s = cJSON_PrintUnformatted(obj);
    if (!s)
        return -1;
    int _nc_rc = nc_write_file_str(path, s);
    free(s);
    return _nc_rc;
}

/* Run a shell command silently, return 0 on success, -1 on failure. */
int nc_exec_quiet(const char *cmd);

/* Check if a command exists in $PATH (no shell spawn). */
bool nc_cmd_exists(const char *name);

/* Resolve HTTP(S) proxy from environment (HTTPS_PROXY, HTTP_PROXY, ALL_PROXY
 * and their lowercase variants).  Returns NULL if none set or empty. */
const char *nc_get_proxy_env(void);

/* Return $HOME (cached after first call).  Returns NULL if unset/empty. */
const char *nc_home_dir(void);

/* Reset nc_home_dir() cache — for tests that override $HOME via setenv. */
void nc_home_dir_reset(void);

/* Return $USER (cached after first call).  Returns NULL if unset/empty. */
const char *nc_user(void);

/* Return $SHELL (cached after first call).  Returns NULL if unset/empty. */
const char *nc_shell_env(void);

/* Return $TMPDIR (cached after first call).  Falls back to "/tmp". */
const char *nc_tmpdir(void);

/* Return $TERM (cached after first call).  Returns NULL if unset/empty. */
const char *nc_term_env(void);

/* Reset all env caches — for tests that override env vars via setenv. */
void nc_env_reset(void);

/* String utilities */
static inline bool str_has_suffix(const char *s, const char *suffix) {
    if (!s || !suffix)
        return false;
    size_t slen = strlen(s), sfxlen = strlen(suffix);
    return slen >= sfxlen && strcmp(s + slen - sfxlen, suffix) == 0;
}

/* Token estimation */
int estimate_tokens(const char *text);
int estimate_message_tokens(const message_t *m);
int estimate_conversation_tokens(const message_t *msgs, int n, const char *sys);

/* Tool result truncation */
char *tool_result_truncate(const char *output, const char *tool_name);
char *tool_result_spill(const char *output, const char *tool_name);
int tool_result_cleanup(void);
int trs_init(const char *project_root);
char *trs_store(const char *call_id, const char *content, size_t len);
char *trs_load(const char *id);
char *trs_maybe_spill(const char *call_id, const char *content);
char *trs_maybe_spill_ex(const char *call_id, const char *content, bool has_delegate);
int trs_count(void);
size_t trs_total_bytes(void);
int trs_cleanup(int max_age_s);
int trs_check_msg_budget(const char **results, const int *lens, int n, int *over_idx);

/* Bash dynamic readOnly detection */
bool bash_is_readonly(const char *cmd);
bool bash_check_readonly(const cJSON *input);
const char *bash_semantic_label(const char *cmd, int exit_code);

/* Memory system */
typedef struct {
    char *key;
    char *value;
    long timestamp;
} memory_entry_t;

typedef struct {
    memory_entry_t *entries;
    int len;
    int cap;
} memory_ctx_t;

void memory_init(memory_ctx_t *m);
void memory_free(memory_ctx_t *m);
int memory_load(memory_ctx_t *m, const char *path);
int memory_save(const memory_ctx_t *m, const char *path);
void memory_set(memory_ctx_t *m, const char *key, const char *value);
int memory_delete(memory_ctx_t *m, const char *key);
const char *memory_get(const memory_ctx_t *m, const char *key);
int memory_count(const memory_ctx_t *m);
int memory_consolidate(memory_ctx_t *m);
char *memory_format_for_prompt(const memory_ctx_t *m);

/* Inline @file / @url expansion into message text */
char *context_inject_refs(const char *input);
int context_inject_images(const char *input, message_t *msg);

/* Image support */
bool is_image_file(const char *path);
char *image_read_base64(const char *path, char **media_type);
const char *image_media_type_for_ext(const char *ext);

typedef struct {
    char *name;
    char *description;
    char *system_prompt;
    char *model;
    int max_turns;
    bool background;
    bool read_only;
    bool auto_accept;
    int effort; /* 0=default, 1=low, 2=medium, 3=high */
    char color; /* r/g/b/y/o/c or 0 for default */
    char *allowed_tools[MAX_AGENT_TOOLS];
    int num_allowed_tools;
    char *disallowed_tools[MAX_AGENT_TOOLS];
    int num_disallowed_tools;
} agent_def_t;

void agent_def_init(agent_def_t *a);
void agent_def_free(agent_def_t *a);
int agent_def_parse_md(agent_def_t *a, const char *content);
bool agent_def_tool_allowed(const agent_def_t *a, const char *tool_name);
const agent_def_t *agent_def_find(const char *name);
int agent_def_list_builtin(const agent_def_t **out, int max);

/* Built-in skills */
const char *skill_find_prompt(const char *name);
int skill_list_all(char **out);
bool skill_exists(const char *name);

/* Context compression (Snip / MicroCompact / Prune) */
int context_snip_message(message_t *m);
int context_estimate_freed_tokens(int freed_chars);
int context_prune_tool_outputs(conversation_t *conv, int keep_recent_rounds);

/* Compression metrics tracking */
void compact_metrics_record(int level, int chars_freed);
char *compact_metrics_format(void);
void compact_metrics_reset(void);
int compact_metrics_total_invocations(void);
long compact_metrics_total_tokens_saved(void);

/* Compact template: iterative summary + budget scaling */
int compact_tpl_summary_budget(int compressed_tokens);
bool compact_tpl_has_prior_summary(const char *text);
char *compact_tpl_build_iterative_prompt(const char *prior_summary, int msg_count, int token_count,
                                         const char *session_id);

/* Path sandboxing (enforcement is opt-in; off by default) */
void sandbox_init(const char *project_root);
void sandbox_enable(void);
void sandbox_disable(void);
bool sandbox_enforcing(void);
bool sandbox_check_path(const char *path);
const char *sandbox_root(void);

/* Cost tracker */
void cost_tracker_init(void);
void cost_tracker_add(const char *model, int in_tok, int out_tok, long api_ms);
void cost_tracker_add_ext(const char *model, int in_tok, int out_tok, int cache_read,
                          int cache_write, int reasoning, long api_ms);
void cost_tracker_add_retry(void);
void cost_tracker_add_lines(int added, int removed);
void cost_tracker_add_tool(const char *tool_name, long exec_ms, bool failed);
int cost_tracker_total_input(void);
int cost_tracker_total_output(void);
int cost_tracker_total_cache_read(void);
int cost_tracker_total_cache_write(void);
int cost_tracker_total_reasoning(void);
double cost_tracker_total_cost(void);
int cost_tracker_total_requests(void);
int cost_tracker_total_retries(void);
int cost_tracker_tool_count(void);
int cost_tracker_total_tool_calls(void);
int cost_tracker_total_tool_fails(void);
char *cost_tracker_format(void);

/* File history / rewind */
void file_history_init(void);
void file_history_free(void);
void file_history_next_turn(void);
int file_history_current_turn(void);
int file_history_count(void);
int file_history_snapshot(const char *path);
bool file_history_has_turn(int turn);
int file_history_unique_turns(int *turns, int max);
int file_history_files_at_turn(int turn, const char **paths, int max);
int file_history_restore_turn(int turn, int *restored, int *errors);
char *file_history_format_checkpoints(void);

/* Tips system */
void tips_init(void);
const char *tips_get_random(void);
int tips_total(void);
const char *tips_get_by_index(int idx);

typedef struct {
    char *paths[READ_FILE_TRACKER_MAX];
    int count;
} read_file_tracker_t;

void read_file_tracker_init(read_file_tracker_t *t);
void read_file_tracker_free(read_file_tracker_t *t);
void read_file_tracker_add(read_file_tracker_t *t, const char *path);
int read_file_tracker_get_recent(const read_file_tracker_t *t, const char **out, int max);

char *compact_restore_files(const read_file_tracker_t *tracker);

/* Message grouping by API round (for compact PTL retry) */
typedef struct {
    int start;
    int end; /* exclusive */
} msg_group_t;

int compact_group_by_api_round(const message_t *msgs, int n, msg_group_t *groups, int max_groups);
int compact_strip_images(message_t *msgs, int n);
int compact_prune_thinking(message_t *msgs, int n, int keep_recent);
char *compact_build_memory_anchor(const char *session_memories, const char *summary);

/* Resume summary */
char *generate_resume_summary(const message_t *msgs, int n, const char *session_name);

/* /files: list files in context */
char *files_in_context(const read_file_tracker_t *tracker);

/* /pr: commit + push + create PR */
char *build_pr_prompt(void);

/* Custom skills from disk (searches all configured skill directories) */
int custom_skills_load(const char *dir);
const char *custom_skill_find(const char *name);
int custom_skill_list(char **names, int max);
void custom_skills_free(void);

/* MCP server management */
char *mcp_format_server_list(const char *config_path);
int mcp_add_server_to_config(const char *path, const char *name, const char *command);
int mcp_remove_server_from_config(const char *path, const char *name);

/* Scratchpad — shared coordinator-worker key-value workspace */
void scratchpad_init(void);
void scratchpad_free(void);
int scratchpad_write(const char *key, const char *value, const char *author);
const char *scratchpad_read(const char *key);
int scratchpad_remove(const char *key);
int scratchpad_count(void);
int scratchpad_keys(const char **out, int max);
char *scratchpad_format(void);
int scratchpad_persist(const char *path);

/* Session mode persistence */
void session_mode_set(int mode);
int session_mode_get(void);
const char *session_mode_name(int mode);
int session_mode_parse(const char *name);
int session_mode_save(const char *session_id);
int session_mode_load(const char *session_id);
bool session_mode_match(const char *session_id);
int session_mode_effort_hint(int mode);
bool session_mode_is_readonly(int mode);
const char *session_mode_context_hint(int mode);

/* Personality presets */
void personality_set(int id);
int personality_get(void);
int personality_parse(const char *name);
const char *personality_name(int id);
const char *personality_prompt(void);
int personality_count(void);
char *personality_list(void);

/* Prompt building */
char *build_system_prompt(void);

/* Project instruction loading */
char *agents_md_load(void);

/* Exec environment normalization for tool subprocesses */
void exec_env_set_active(bool active);
bool exec_env_is_active(void);
void exec_env_build_array(char **envp, int max, int *out_count);
void exec_env_free_array(char **envp, int count);
int exec_env_pair_count(void);
const char *exec_env_key_at(int idx);
const char *exec_env_value_at(int idx);

/* ApplyPatch tool — apply unified diff patches */
int apply_patch_execute(const cJSON *input, char **output, void *ctx);

typedef enum {
    AGENT_STATUS_RUNNING = 0,
    AGENT_STATUS_COMPLETED = 1,
    AGENT_STATUS_FAILED = 2,
    AGENT_STATUS_PAUSED = 3
} agent_status_t;

const char *agent_status_name(agent_status_t s);

typedef struct {
    char *agent_id;
    char *task;
    char *agent_type;
    pid_t pid;
    int pipe_out;
    int pipe_in; /* write-end of agent's stdin for SendMessage */
    int status;  /* agent_status_t */
    char *result;
    long start_time;
} live_agent_t;

typedef struct {
    live_agent_t agents[MAX_LIVE_AGENTS];
    int count;
} agent_pool_t;

/* Agent fork mode — controls what history a sub-agent inherits */
typedef enum { FORK_NONE = 0, FORK_FULL = 1, FORK_LAST_N = 2 } agent_fork_mode_t;

typedef struct {
    agent_fork_mode_t mode;
    int last_n;               /* only for FORK_LAST_N */
    const char *context_hint; /* optional extra context for the fork */
} agent_spawn_opts_t;

void agent_pool_init(agent_pool_t *p);
void agent_pool_free(agent_pool_t *p);
agent_pool_t *agent_pool_global(void);
char *agent_pool_spawn(agent_pool_t *p, const char *task, const char *agent_type, const void *cfg);
char *agent_pool_spawn_ex(agent_pool_t *p, const char *task, const char *agent_type,
                          const void *cfg, const agent_spawn_opts_t *opts);
int agent_pool_list(agent_pool_t *p, char **output);
int agent_pool_wait(agent_pool_t *p, const char *agent_id, char **output);
int agent_pool_close(agent_pool_t *p, const char *agent_id, char **output);
int agent_pool_count(const agent_pool_t *p);
int agent_pool_active_count(agent_pool_t *p);
int agent_pool_partial_output(agent_pool_t *p, const char *agent_id, char **output, int *running);

/* Turn timing — per-turn API call duration tracking */
void turn_timing_init(void);
void turn_timing_start(void);
long turn_timing_elapsed_ms(void);
void turn_timing_record(int turn, long api_ms, long tool_ms, int tool_count, int in_tok,
                        int out_tok);
void turn_timing_record_ex(int turn, long api_ms, long tool_ms, long ttft_ms, int tool_count,
                           int in_tok, int out_tok);
int turn_timing_count(void);
char *turn_timing_format(void);
long turn_timing_total_api_ms(void);
long turn_timing_total_tool_ms(void);

/* Turn diff tracker — track file changes per turn with baseline snapshots */
void turn_diff_init(void);
void turn_diff_free(void);
void turn_diff_snapshot_baseline(const char *path, int turn);
void turn_diff_track_rename(const char *old_path, const char *new_path);
void turn_diff_record(int turn, const char *path, int lines_added, int lines_removed);
char *turn_diff_format(int turn);
char *turn_diff_format_all(void);
char *turn_diff_unified(const char *path);
char *turn_diff_unified_all(void);
int turn_diff_total_added(void);
int turn_diff_total_removed(void);
int turn_diff_baseline_count(void);
int turn_diff_files_changed(void);

/* Streaming Tool Executor — execute tools as they arrive during streaming */
typedef int (*tool_exec_fn)(const tool_def_t *tool, cJSON *input, char **output, void *ctx);
typedef struct streaming_exec streaming_exec_t;

void streaming_exec_init(streaming_exec_t *se);
void streaming_exec_reset(streaming_exec_t *se);
streaming_exec_t *streaming_exec_global(void);
void streaming_exec_tool_begin(streaming_exec_t *se, const char *id, const char *name);
void streaming_exec_tool_delta(streaming_exec_t *se, const char *json_frag);
void streaming_exec_tool_end(streaming_exec_t *se);
int streaming_exec_ready_count(const streaming_exec_t *se);
int streaming_exec_total(const streaming_exec_t *se);
int streaming_exec_completed(const streaming_exec_t *se);
bool streaming_exec_all_done(const streaming_exec_t *se);
const char *streaming_exec_tool_id(const streaming_exec_t *se, int idx);
const char *streaming_exec_tool_name(const streaming_exec_t *se, int idx);
const char *streaming_exec_tool_args(const streaming_exec_t *se, int idx);
const char *streaming_exec_tool_output(const streaming_exec_t *se, int idx);
int streaming_exec_tool_rc(const streaming_exec_t *se, int idx);
bool streaming_exec_tool_done(const streaming_exec_t *se, int idx);

/* Project Root Auto-Detection */
const char *project_root_detect(const char *start_dir);
void project_root_reset(void);
int project_root_markers_at(const char *dir, const char **out, int max);
const char *project_root_primary_marker(const char *dir);
char *project_root_describe(void);
bool project_root_is_git(void);
const char *project_root_list_markers(void);

typedef enum { NET_ACTION_ALLOW, NET_ACTION_DENY, NET_ACTION_ASK } net_action_t;

typedef struct {
    char *pattern;
    net_action_t action;
} net_rule_t;

typedef struct {
    net_rule_t rules[MAX_NET_RULES];
    int num_rules;
    net_action_t default_action;
} network_policy_t;

void network_policy_init(network_policy_t *p);
void network_policy_free(network_policy_t *p);
int network_policy_add_rule(network_policy_t *p, const char *pattern, net_action_t action);
net_action_t network_policy_check(const network_policy_t *p, const char *host);
net_action_t network_policy_check_url(const network_policy_t *p, const char *url);
net_action_t network_policy_check_cmd(const network_policy_t *p, const char *cmd);
char *network_policy_extract_urls_from_cmd(const char *cmd);
int network_policy_load(network_policy_t *p, const char *path);
network_policy_t *network_policy_global(void);
char *network_policy_format(const network_policy_t *p);

/* Thread Rollout Truncation — smart conversation truncation */
int thread_truncate_rounds(message_t *msgs, int n, int rounds_to_remove);
int thread_truncate_to_token_budget(message_t *msgs, int n, const char *sys_prompt, int budget);
int thread_find_safe_cut(const message_t *msgs, int n, int desired_cut);
int thread_truncate_safe(message_t *msgs, int n, int rounds_to_remove);
char *thread_truncation_describe(const message_t *msgs, int n);

/* Tool Suggest — input analysis and tool recommendation */
typedef struct {
    const char *tool_name;
    const char *reason;
    double confidence;
} tool_suggestion_t;

int tool_suggest_for_input(const char *input, tool_suggestion_t *out, int max);
char *tool_suggest_format(const tool_suggestion_t *suggestions, int n);
int tool_suggest_exec(const cJSON *input, char **output, void *ctx);

/* Fuzzy Match — case-insensitive subsequence matching */
typedef struct {
    const char *item;
    int index;
    int score; /* lower = better */
} fuzzy_result_t;

int fuzzy_match(const char *haystack, const char *needle, int *indices, int max_indices,
                int *score_out);
int fuzzy_score(const char *haystack, const char *needle);
int fuzzy_filter(const char **items, int count, const char *query, fuzzy_result_t *results,
                 int max_results);

/* ── Edit tool fuzzy matching (shared by Edit and MultiEdit) ── */
typedef char *(*edit_norm_fn_t)(const char *);

char *edit_rstrip_lines(const char *s);
char *edit_trim_lines(const char *s);
char *edit_collapse_ws(const char *s);
char *edit_strip_indent(const char *s);
char *edit_unicode_norm(const char *s);

extern const edit_norm_fn_t edit_norm_fns[EDIT_FUZZY_MATCH_LEVELS];
extern const char *edit_level_names[EDIT_FUZZY_MATCH_LEVELS];

bool edit_fuzzy_locate(const char *content, const char *old_str, size_t old_len, size_t *out_offset,
                       size_t *out_match_len, int *out_level);
int edit_fuzzy_count(const char *content, const char *old_str, size_t old_len, int level);

/* History persistence */
typedef enum {
    HISTORY_FILE_SNAPSHOT_ERROR = -1,
    HISTORY_FILE_SNAPSHOT_NONE = 0,
    HISTORY_FILE_SNAPSHOT_RESTORED = 1,
    HISTORY_FILE_SNAPSHOT_SKIPPED_DIRTY = 2,
    HISTORY_FILE_SNAPSHOT_UNAVAILABLE = 3
} history_file_snapshot_status_t;

int history_save(const conversation_t *c, const char *id);
int history_load(conversation_t *c, const char *id);
int nc_session_history_list(char **out);
char *history_gen_session_id(void);
char *history_find_latest(void);
int history_rename(const char *session_id, const char *name);
int history_search_fuzzy(const char *query, char **out);
int history_attach_file_snapshot(const char *session_id, const char *snapshot_ref);
history_file_snapshot_status_t history_restore_file_snapshot(const char *session_id);
char *history_file_snapshot_status_message(const char *session_id,
                                           history_file_snapshot_status_t status);

/* Auto Session Title */
char *session_title_extract(const char *user_msg);
int session_title_auto(const char *session_id, const char *user_msg);

/* Persistent Shell Session */
void shell_session_init(void);
void shell_session_cleanup(void);
int shell_session_exec(const char *cmd, const char *workdir, int timeout_s, char **output,
                       int *exit_code);
bool shell_session_active(void);
void shell_session_reset(void);

/* HeadTailBuffer — streaming output with head+tail preservation */
typedef struct {
    char *head;
    size_t head_len;
    size_t head_budget;
    char *tail;
    size_t tail_len;
    size_t tail_budget;
    size_t max_bytes;
    size_t omitted_bytes;
} head_tail_buf_t;

void htbuf_init(head_tail_buf_t *b, size_t max_bytes);
void htbuf_free(head_tail_buf_t *b);
void htbuf_push(head_tail_buf_t *b, const char *data, size_t len);
size_t htbuf_retained(const head_tail_buf_t *b);
size_t htbuf_omitted(const head_tail_buf_t *b);
char *htbuf_finalize(const head_tail_buf_t *b);
char *htbuf_finalize_with_meta(const head_tail_buf_t *b, size_t *total, size_t *omitted);

/* Git-based Undo via stash */
void git_undo_init(void);
void git_undo_free(void);
bool git_undo_available(void);
bool git_undo_has_turn(int turn);
int git_undo_capture_turn(int turn, const char *description);
int git_undo_restore(int turn);
int git_undo_count(void);
char *git_undo_format_list(void);

/* Coordinator Mode — multi-agent orchestration */
void coordinator_init(void);
bool coordinator_active(void);
void coordinator_set_active(bool active);
const char *coordinator_system_prompt(void);
bool coordinator_tool_allowed(const char *name);
void coordinator_reset_tasks(void);
int coordinator_add_task(const char *description);
int coordinator_assign_task(int idx, const char *agent_id);
int coordinator_complete_task(int idx, const char *result, bool success);
int coordinator_find_task_by_agent(const char *agent_id);
int coordinator_pending_count(void);
int coordinator_completed_count(void);
char *coordinator_format_status(void);

/* SendMessage — inter-agent messaging */
int agent_pool_send_message(agent_pool_t *p, const char *agent_id, const char *message);

typedef enum { EXEC_ALLOW, EXEC_DENY, EXEC_PROMPT } exec_decision_t;

typedef struct {
    char *prefix;
    exec_decision_t decision;
    char *justification;
} exec_rule_t;

typedef struct {
    exec_rule_t rules[MAX_EXEC_RULES];
    int num_rules;
    exec_decision_t default_decision;
} exec_rules_ctx_t;

void exec_rules_init(exec_rules_ctx_t *r);
void exec_rules_free(exec_rules_ctx_t *r);
int exec_rules_add(exec_rules_ctx_t *r, const char *prefix, exec_decision_t decision,
                   const char *justification);
int exec_rules_parse(exec_rules_ctx_t *r, const char *content);
int exec_rules_load_file(exec_rules_ctx_t *r, const char *path);
int exec_rules_load_dir(exec_rules_ctx_t *r, const char *dir);
int exec_rules_load_from_config(exec_rules_ctx_t *r);
exec_decision_t exec_rules_check(const exec_rules_ctx_t *r, const char *command,
                                 const char **justification_out);
exec_rules_ctx_t *exec_rules_global(void);
char *exec_rules_format(const exec_rules_ctx_t *r);

/* Structured Review Findings — code review output formatting */
typedef enum {
    REVIEW_INFO = 0,
    REVIEW_WARNING = 1,
    REVIEW_ERROR = 2,
    REVIEW_CRITICAL = 3
} review_severity_t;

typedef struct {
    char *title;
    char *body;
    char *file_path;
    int start_line;
    int end_line;
    review_severity_t severity;
} review_finding_t;

void review_finding_init(review_finding_t *f);
void review_finding_free(review_finding_t *f);
void review_findings_free(review_finding_t *findings, int count);
int review_finding_set(review_finding_t *f, const char *title, const char *body,
                       const char *file_path, int start_line, int end_line,
                       review_severity_t severity);
char *review_format_finding(const review_finding_t *f, int index, const bool *selected);
char *review_format_findings(const review_finding_t *findings, int count, const bool *selection);
char *review_format_summary(const char *explanation, const review_finding_t *findings, int count);
int review_count_by_severity(const review_finding_t *findings, int count,
                             review_severity_t severity);

/* RequestPermissions tool — model requests additional permissions at runtime */
int request_perms_execute(const cJSON *input, char **output, void *ctx);

char *cmd_canonicalize(const char *cmd);
bool cmd_canonical_equal(const char *a, const char *b);
void approval_cache_init(void);
void approval_cache_free(void);
void approval_cache_add(const char *cmd);
bool approval_cache_check(const char *cmd);
int approval_cache_count_entries(void);

/* Commit Attribution — Co-authored-by trailer for AI-assisted commits */
void commit_attr_init(const char *attribution);
const char *commit_attr_value(void);
char *commit_attr_trailer(void);
char *commit_attr_instruction(void);
bool commit_msg_has_trailer(const char *msg);
char *commit_msg_ensure_trailer(const char *msg);

/* Shell Detection — auto-detect user shell type */
typedef enum {
    SHELL_UNKNOWN = 0,
    SHELL_ZSH,
    SHELL_BASH,
    SHELL_SH,
    SHELL_FISH,
    SHELL_DASH,
    SHELL_KSH,
    SHELL_CSH,
    SHELL_TCSH
} shell_type_t;

shell_type_t shell_detect_from_path(const char *path);
const char *shell_type_name(shell_type_t type);
const char *shell_init_file(shell_type_t type);
const char *shell_prompt_var(shell_type_t type);
bool shell_supports_source(shell_type_t type);
void shell_detect_init(void);
shell_type_t shell_detected(void);
const char *shell_detected_path(void);
char *shell_detect_describe(void);

/* Text Encoding Safety — encoding detection and binary protection */
bool text_is_binary_data(const unsigned char *data, size_t len);
bool text_is_binary_file(const char *path);
const char *text_encoding_name(const unsigned char *data, size_t len);
size_t text_valid_utf8_prefix(const unsigned char *data, size_t len);
char *text_sanitize_for_display(const unsigned char *data, size_t len);
char *text_strip_surrogates(const char *input, size_t len);
bool text_has_surrogates(const char *data, size_t len);

/* Exec Rules Persistence — save approved commands to rules file */
int exec_rules_persist_allow(exec_rules_ctx_t *r, const char *prefix, const char *justification);

/* Middle Truncation — UTF-8 safe head+tail truncation */
int approx_token_count(const char *text);
size_t approx_bytes_for_tokens(int tokens);
char *middle_truncate_bytes(const char *s, size_t max_bytes);
char *middle_truncate_tokens(const char *s, int max_tokens);
char *tool_result_truncate_middle(const char *output, const char *tool_name, int max_tokens);

/* Sleep Inhibitor — native IOKit sleep prevention */
void sleep_inhibit_acquire(void);
void sleep_inhibit_release(void);
bool sleep_inhibit_active(void);
void sleep_inhibit_set_enabled(bool enabled);
void sleep_inhibit_set_turn(bool running);
bool sleep_inhibit_is_turn_running(void);

/* Security Presets — rich preset metadata */
typedef struct {
    const char *id;
    const char *label;
    const char *description;
    int approval_value; /* maps to approval_preset_t enum */
} security_preset_t;

int security_presets_list(const security_preset_t **out, int max);
const security_preset_t *security_preset_find(const char *id);
int security_preset_count(void);
char *security_presets_format(void);

/* REPL Tool — Python/Node/Ruby interactive sessions */
int repl_tool_exec(const cJSON *input, char **output, void *ctx);
void repl_cleanup_all(void);
int repl_active_count(void);
char *repl_list_sessions(void);

/* Review Integration — structured review prompts */
char *build_review_prompt(const char *target, const char *focus);
char *build_security_review_prompt(const char *target);
int review_parse_severity_tag(const char *line);
int review_parse_file_line(const char *line, char *path, size_t path_sz, int *start, int *end);

typedef enum { WATCH_FILE, WATCH_DIR } file_watch_type_t;

typedef void (*file_watch_cb)(const char *path, file_watch_type_t type, void *ctx);

typedef struct {
    char *path;
    int fd;
    file_watch_type_t type;
    time_t last_mtime;
} file_watch_entry_t;

typedef struct {
    file_watch_entry_t watches[MAX_WATCHES];
    int num_watches;
    file_watch_cb callbacks[MAX_WATCH_CALLBACKS];
    void *callback_ctx[MAX_WATCH_CALLBACKS];
    int num_callbacks;
    int kq_fd;
    bool running;
    pthread_t thread;
    int debounce_ms;
} file_watcher_t;

void file_watcher_init(file_watcher_t *w);
void file_watcher_free(file_watcher_t *w);
int file_watcher_add(file_watcher_t *w, const char *path, file_watch_type_t type);
int file_watcher_add_callback(file_watcher_t *w, file_watch_cb cb, void *ctx);
int file_watcher_start(file_watcher_t *w);
void file_watcher_stop(file_watcher_t *w);
bool file_watcher_active(const file_watcher_t *w);
int file_watcher_count(const file_watcher_t *w);
const char *file_watcher_path(const file_watcher_t *w, int idx);
int file_watcher_check_poll(file_watcher_t *w);
file_watcher_t *file_watcher_global(void);
int file_watcher_add_config_paths(file_watcher_t *w, const char *project_root);

typedef enum {
    PROCGROUP_TOOL = 0,
    PROCGROUP_AGENT = 1,
    PROCGROUP_REPL = 2,
    PROCGROUP_MCP = 3,
    PROCGROUP_OTHER = 4
} procgroup_type_t;

typedef struct {
    pid_t pid;
    char *label;
    procgroup_type_t type;
    time_t start_time;
    bool exited;
    int exit_code;
} procgroup_entry_t;

typedef struct {
    procgroup_entry_t entries[MAX_PROCGROUP];
    int num_entries;
} procgroup_ctx_t;

void procgroup_init(procgroup_ctx_t *pg);
void procgroup_free(procgroup_ctx_t *pg);
int procgroup_register(procgroup_ctx_t *pg, pid_t pid, const char *label, procgroup_type_t type);
int procgroup_unregister(procgroup_ctx_t *pg, pid_t pid);
int procgroup_signal_all(procgroup_ctx_t *pg, int sig);
int procgroup_reap(procgroup_ctx_t *pg);
int procgroup_cleanup_exited(procgroup_ctx_t *pg);
int procgroup_graceful_shutdown(procgroup_ctx_t *pg, int timeout_s);
int procgroup_count(const procgroup_ctx_t *pg);
int procgroup_active_count(const procgroup_ctx_t *pg);
int procgroup_count_by_type(const procgroup_ctx_t *pg, procgroup_type_t type);
bool procgroup_has_pid(const procgroup_ctx_t *pg, pid_t pid);
long procgroup_elapsed_s(const procgroup_ctx_t *pg, pid_t pid);
char *procgroup_format(const procgroup_ctx_t *pg);
procgroup_ctx_t *procgroup_global(void);

typedef enum { DIAG_ERROR = 0, DIAG_WARNING = 1, DIAG_NOTE = 2, DIAG_HINT = 3 } diag_severity_t;

typedef struct {
    char *file;
    int line;
    int column;
    diag_severity_t severity;
    char *message;
    char *suggestion;
    char *code;
} diagnostic_entry_t;

void diagnostic_init(diagnostic_entry_t *d);
void diagnostic_free(diagnostic_entry_t *d);
void diagnostics_free(diagnostic_entry_t *diags, int count);
int diagnostic_parse_output(const char *output, diagnostic_entry_t *diags, int max_diags);
const char *diagnostic_suggest_fix(const diagnostic_entry_t *d);
int diagnostic_annotate_suggestions(diagnostic_entry_t *diags, int count);
char *diagnostic_format(const diagnostic_entry_t *diags, int count);
int diagnostic_count_errors(const diagnostic_entry_t *diags, int count);
int diagnostic_count_warnings(const diagnostic_entry_t *diags, int count);
char *diagnostic_summary(const diagnostic_entry_t *diags, int count);

/* Hot-Reload — file watcher triggered config reloading */
typedef enum {
    HOT_RELOAD_AGENTS_MD = 0,
    HOT_RELOAD_HOOKS = 1,
    HOT_RELOAD_MEMORY = 2,
    HOT_RELOAD_SKILLS = 3,
    HOT_RELOAD_RULES = 4,
    HOT_RELOAD_MCP = 5,
    HOT_RELOAD_SETTINGS = 6
} hot_reload_kind_t;

void hot_reload_flag_set(hot_reload_kind_t kind);
int hot_reload_flags_consume(void);
bool hot_reload_pending(void);
const char *hot_reload_kind_name(hot_reload_kind_t kind);
void hot_reload_watcher_cb(const char *path, file_watch_type_t type, void *ctx);
int hot_reload_process(int flags, hook_ctx_t *hooks, memory_ctx_t *memory, void *conv);

/* Structured Output / JSON Mode */
cJSON *structured_output_wrap_request(const cJSON *base_body, const cJSON *schema,
                                      provider_type_t provider);
char *structured_output_inject_prompt(const char *system_prompt, const cJSON *schema);
bool structured_output_validate(const char *output, const cJSON *schema);
char *structured_output_extract_json(const char *response);

/* Enhanced API Retry — exponential backoff with jitter + error classification */
int api_retry_stream(int (*stream_fn)(const message_t *, int, const char *, const void *,
                                      const tool_def_t *, int, stream_callback_t, void *),
                     const message_t *msgs, int n, const char *sys, const void *cfg,
                     const tool_def_t *tools, int nt, stream_callback_t cb, void *ud);
int api_retry_classify(const char *error_msg);
bool api_retry_should_retry(int error_class);
int api_retry_delay_ms(int attempt, int error_class);

/* Conversation Recovery — crash/interrupt auto-save and restart recovery */
void conv_recovery_init(void);
bool conv_recovery_enabled(void);
const char *conv_recovery_path(void);
int conv_recovery_save(const void *conv, const char *session_id);
bool conv_recovery_exists(void);
int conv_recovery_load(void *conv, char **session_id_out);
void conv_recovery_clear(void);
long conv_recovery_age_seconds(void);
char *conv_recovery_describe(void);

/* Token Budget Per Turn — output token tracking + auto-continuation */
void token_budget_init(int budget);
void token_budget_start_turn(void);
void token_budget_set(int budget);
int token_budget_get(void);
bool token_budget_active(void);
int token_budget_continuation_count(void);
long token_budget_elapsed_s(void);
char *token_budget_continuation_msg(int attempt, int total_attempts);
char *token_budget_format(int turn_output_tokens);

/* Conversation Branching — enhanced /branch with git stash snapshots */
void conv_branch_init(void);
void conv_branch_free(void);
int conv_branch_create(const char *session_id, const char *name, int msg_count);
int conv_branch_count(void);
const char *conv_branch_session_id(int idx);
const char *conv_branch_name(int idx);
const char *conv_branch_snapshot_ref(int idx);
bool conv_branch_has_snapshot(int idx);
int conv_branch_msg_count(int idx);
char *conv_branch_format_list(void);
int conv_branch_find(const char *query);

/* Mention Syntax — enhanced @-references (@url, @~path, @dir/) */
bool mention_is_url(const char *s);
char *mention_fetch_url(const char *url);
char *mention_parse_enhanced(const char *input);
int mention_count_refs(const char *input);

/* Model Routing — smart model selection based on task complexity */
typedef enum {
    TASK_SIMPLE = 0,
    TASK_MODERATE = 1,
    TASK_COMPLEX = 2,
    TASK_CREATIVE = 3
} task_complexity_t;

void model_router_init(void);
void model_router_free(void);
int model_router_add_tier(const char *model, int min_complexity);
bool model_router_enabled(void);
task_complexity_t model_router_classify(const char *input);
const char *model_router_select(const char *input);
const char *model_router_complexity_name(task_complexity_t c);
char *model_router_describe(const char *input);
char *model_router_format_tiers(void);
int model_router_load_from_settings(const cJSON *settings);

/* Multi-provider Fallback — automatic provider switch on failure */
void multi_provider_init(void);
void multi_provider_free(void);
int multi_provider_add(const char *base_url, const char *api_key, const char *model,
                       provider_type_t type);
int multi_provider_count(void);
bool multi_provider_has_fallback(void);
const char *multi_provider_url(int idx);
const char *multi_provider_key(int idx);
const char *multi_provider_model(int idx);
provider_type_t multi_provider_type(int idx);
bool multi_provider_active(int idx);
void multi_provider_disable(int idx);
void multi_provider_enable(int idx);
void multi_provider_enable_all(void);
int multi_provider_load_from_env(void);
int multi_provider_load_from_config(const cJSON *root);
char *multi_provider_format(void);

/* Input preprocessing — length/binary sanity only */
typedef enum { INPUT_OK = 0, INPUT_EMPTY = 1, INPUT_TOO_LONG = 2, INPUT_BINARY = 3 } input_check_t;

input_check_t input_check(const char *input);
const char *input_check_name(input_check_t check);
bool input_check_is_safe(input_check_t check);
char *input_strip_control_chars(const char *input);
char *input_collapse_whitespace(const char *input);
char *input_trim(const char *input);
char *input_preprocess(const char *raw_input, input_check_t *check_out);
bool input_is_warn_length(const char *input);
size_t input_max_length(void);

/* Session Statistics Persistence — cumulative cross-session tracking */
void session_stats_init(void);
void session_stats_free(void);
int session_stats_load(void);
int session_stats_save(void);
void session_stats_record_session(const char *model, long input_tokens, long output_tokens,
                                  double cost, long duration_s, long api_calls, int tool_uses,
                                  int lines_added, int lines_removed);
int session_stats_total_sessions(void);
long session_stats_total_tokens(void);
double session_stats_total_cost(void);
long session_stats_total_duration(void);
int session_stats_model_count(void);
char *session_stats_format(void);
char *session_stats_format_brief(void);

typedef enum { CTX_SEV_INFO = 0, CTX_SEV_WARNING = 1 } ctx_severity_t;

typedef struct {
    ctx_severity_t severity;
    char *title;
    char *detail;
    int savings_tokens;
} ctx_suggestion_t;

typedef struct {
    char name[NC_FMT_MAX];
    int call_tokens;
    int result_tokens;
} ctx_tool_stat_t;

typedef struct {
    int context_limit;
    int total_tokens;
    int usage_pct;
    int memory_tokens;
    bool autocompact_enabled;
    ctx_tool_stat_t tool_stats[MAX_CTX_TOOL_STATS];
    int num_tool_stats;
} ctx_analysis_t;

void ctx_suggestion_init(ctx_suggestion_t *s);
void ctx_suggestion_free(ctx_suggestion_t *s);
void ctx_suggestions_free(ctx_suggestion_t *arr, int n);
int ctx_suggest_analyze(const ctx_analysis_t *a, ctx_suggestion_t *out, int max);
void ctx_analysis_from_conversation(ctx_analysis_t *a, const void *conv, int context_limit,
                                    int memory_tokens, bool autocompact_enabled);
char *ctx_suggest_format(const ctx_suggestion_t *suggestions, int n);

/* Effort Level — thinking budget / reasoning effort control */
typedef enum {
    EFFORT_DEFAULT = 0,
    EFFORT_NONE = 1,
    EFFORT_MINIMAL = 2,
    EFFORT_LOW = 3,
    EFFORT_MEDIUM = 4,
    EFFORT_HIGH = 5,
    EFFORT_XHIGH = 6,
    EFFORT_MAX = 7
} effort_level_t;

void effort_level_init(void);
void effort_level_set(effort_level_t level);
effort_level_t effort_level_get(void);
const char *effort_level_name(effort_level_t level);
const char *effort_level_glyph(effort_level_t level);
effort_level_t effort_level_parse(const char *s);
effort_level_t effort_level_cycle(effort_level_t current);
int effort_level_max_tokens(effort_level_t level, const char *model);
const char *effort_level_compat_value(effort_level_t level);
void effort_apply_to_compat_body(cJSON *body, effort_level_t level, const char *model);
void effort_apply_to_native_body(cJSON *body, effort_level_t level, const char *model,
                                 int max_tokens);
char *effort_level_describe(void);
bool effort_level_valid_for_model(effort_level_t level, const char *model);

/* Away Summary — "while you were away" session recap */
void away_summary_init(void);
void away_summary_touch(void);
void away_summary_set_enabled(bool enabled);
bool away_summary_enabled(void);
long away_summary_idle_seconds(void);
bool away_summary_should_generate(void);
char *away_summary_generate(const void *conv, const char *memory_text, const char *base_url,
                            const char *api_key, const char *model, provider_type_t provider);
char *away_summary_format(const char *summary);
const char *away_summary_prompt_text(void);

/* Workspace Startup Context — bounded project layout for system prompt */
char *workspace_context_build(const char *cwd);
int workspace_context_tree_lines(const char *root, char *buf, size_t buf_sz);

/* No rate limiting — nevercode operates in freedom mode */

/* ── Session Resume — full session reconstruction from persistent state ── */

void session_resume_init(void);
void session_resume_free(void);
bool session_resume_ready(void);
const char *session_resume_summary(void);
const char *session_resume_reference(void);
int session_resume_msgs_restored(void);
long session_resume_elapsed_ms(void);
bool session_resume_memory_loaded(void);
char *session_resume_format(void);

/* ── Session Status (state machine: idle/busy/retry/error/compact/handoff) ── */

void session_status_init(void);
void session_status_set_busy(void);
void session_status_set_idle(void);
void session_status_set_retry(int attempt, const char *reason, long next_ms);
void session_status_set_error(const char *reason);
void session_status_set_compact(void);
void session_status_set_handoff(void);
bool session_status_is_busy(void);
bool session_status_is_idle(void);
const char *session_status_name(void);
int session_status_retry_attempt(void);
long session_status_idle_seconds(void);
char *session_status_format(void);

/* ── Pinned Messages — compaction resistance ── */

void pinned_msg_init(void);
void pinned_msg_set_session(const char *sid);
bool pinned_msg_pin(int idx);
bool pinned_msg_unpin(int idx);
bool pinned_msg_toggle(int idx);
bool pinned_msg_is_pinned(int idx);
int pinned_msg_count(void);
void pinned_msg_clear(void);
void pinned_msg_shift(int removed_before, int n_removed);
int pinned_msg_lowest_pinned(void);
char *pinned_msg_format(int conv_len);
int pinned_msg_save(void);
int pinned_msg_load(const char *sid);

/* ── Input History — persistent across sessions ── */

void input_history_init(void);
void input_history_free(void);
void input_history_push(const char *text);
const char *input_history_up(void);
const char *input_history_down(void);
void input_history_reset_cursor(void);
int input_history_count(void);
int input_history_save(void);
int input_history_load(void);

/* ── System Diagnostics — comprehensive health checks ── */

void diagnostics_init(void);
void sys_diagnostics_free(void);
int diagnostics_run(void);
int diagnostics_pass_count(void);
int diagnostics_warn_count(void);
int diagnostics_fail_count(void);
int diagnostics_total(void);
long diagnostics_elapsed_ms(void);
char *diagnostics_format(void);
int diagnostics_exec(const cJSON *input, char **output, void *ctx);

/* ── Request Compression — gzip payload compression for API calls ── */

void request_compress_init(void);
void request_compress_auto_configure(const char *base_url);
void request_compress_set_enabled(bool on);
bool request_compress_enabled(void);
void request_compress_set_threshold(int bytes);
void request_compress_set_level(int level);
int request_compress_gzip(const char *input, size_t input_len, char **output, size_t *output_len);
bool request_compress_should_compress(size_t payload_size);
int request_compress_maybe(const char *body, size_t body_len, char **out_body, size_t *out_len,
                           bool *was_compressed);
double request_compress_ratio(void);
long request_compress_saved_bytes(void);
int request_compress_call_count(void);
int request_compress_skip_count(void);
char *request_compress_format(void);

/* ── Config Schema — configuration validation ── */

void config_schema_init(void);
int config_schema_validate(const cJSON *config);
int config_schema_error_count(void);
const char *config_schema_error_path(int idx);
const char *config_schema_error_msg(int idx);
char *config_schema_format_errors(void);

/* ── Abort Handler — graceful task abort with cascading cleanup ── */

typedef enum {
    ABORT_USER_REQ = 0,
    ABORT_TIMEOUT_REQ,
    ABORT_ERROR_REQ,
    ABORT_RESOURCE_REQ,
    ABORT_SIGNAL_REQ
} abort_reason_t;

void abort_handler_init(void);
void abort_handler_free(void);
int abort_request(const char *task_id, abort_reason_t reason);
void abort_global(abort_reason_t reason);
bool abort_is_global(void);
bool abort_is_pending(const char *task_id);
int abort_pending_count(void);
int abort_total_count(void);
int abort_forced_count(void);
char *abort_format_status(void);

/* ── OS Sandbox — native kernel sandboxing (seatbelt/landlock) ── */

typedef enum {
    SANDBOX_PROFILE_FULL_ACCESS = 0,
    SANDBOX_PROFILE_READONLY,
    SANDBOX_PROFILE_NO_NETWORK,
    SANDBOX_PROFILE_LOCKDOWN
} sandbox_profile_t;

void os_sandbox_init(void);
void os_sandbox_free(void);
bool os_sandbox_available(void);
const char *os_sandbox_type_name(void);
int os_sandbox_add_path(const char *path, bool writable);
void os_sandbox_set_network(bool allowed);
void os_sandbox_set_subprocess(bool allowed);
bool os_sandbox_active(void);
int os_sandbox_applied_count(void);
int os_sandbox_path_count(void);
const char *os_sandbox_profile_name(sandbox_profile_t p);
char *os_sandbox_format(void);
void os_sandbox_setup_paths(const char *project_root);
int os_sandbox_apply(void); /* call in child after fork(), before exec() */

/* ── Provider Auth — API authentication and token refresh ── */

void provider_auth_init(void);
void provider_auth_free(void);
int provider_auth_set_key(const char *provider_id, const char *key);
int provider_auth_set_bearer(const char *provider_id, const char *token, long expires_at);
int provider_auth_set_refresh(const char *provider_id, const char *refresh_url,
                              const char *refresh_token);
bool provider_auth_needs_refresh(const char *provider_id);
int provider_auth_refresh(const char *provider_id);
const char *provider_auth_credential(const char *provider_id);
bool provider_auth_valid(const char *provider_id);
const char *provider_auth_header_name(const char *provider_id);
char *provider_auth_header_value(const char *provider_id);
int provider_auth_count(void);
int provider_auth_total_refreshes(void);
char *provider_auth_format(void);

/* ── Key Pool — multi-credential rotation with failover ── */

void key_pool_init(void);
void key_pool_free(void);
int key_pool_create(const char *label, int strategy);
int key_pool_add_key(const char *label, const char *key);
const char *key_pool_next(const char *label);
void key_pool_report_failure(const char *label, const char *key, int http_status);
void key_pool_clear_cooldown(const char *label, const char *key);
int key_pool_available_count(const char *label);
int key_pool_total_count(const char *label);
int key_pool_pool_count(void);
int key_pool_total_rotations(void);
int key_pool_total_cooldowns(void);
char *key_pool_format(void);
int key_pool_load_from_config(const cJSON *cfg);

/* ── Swarm Backend Registry — pluggable multi-agent spawn backends ── */

void swarm_backend_init(void);
void swarm_backend_free(void);
int swarm_backend_detect(void);
int swarm_backend_spawn(const char *task, const char *model, int max_turns, char *agent_id_out,
                        size_t id_sz);
int swarm_backend_count(void);
int swarm_backend_available_count(void);
int swarm_backend_total_spawned(void);
const char *swarm_backend_active_name(void);
char *swarm_backend_format(void);

/* ── Git Filesystem — gitignore integration ── */

void git_fs_init(void);
void git_fs_free(void);
int git_fs_load_gitignore(const char *path);
bool git_fs_is_ignored(const char *relpath);
int git_fs_rule_count(void);
bool git_fs_loaded(void);
char *git_fs_format(void);

/* ── Stream Controller — chunk-based streaming with flow control ── */

void stream_ctrl_init(void);
void stream_ctrl_free(void);
void stream_ctrl_reset(void);
int stream_ctrl_begin(void);
int stream_ctrl_feed(const char *data, int len, bool is_final);
int stream_ctrl_commit(void);
bool stream_ctrl_should_commit(void);
const char *stream_ctrl_assembled(void);
int stream_ctrl_assembled_len(void);
void stream_ctrl_pause(void);
void stream_ctrl_resume(void);
void stream_ctrl_cancel(void);
bool stream_ctrl_complete(void);
bool stream_ctrl_active(void);
long stream_ctrl_ttfb_ms(void);
long stream_ctrl_elapsed_ms(void);
int stream_ctrl_chunk_count(void);
int stream_ctrl_pending_count(void);
int stream_ctrl_total_bytes(void);
char *stream_ctrl_format(void);

/* ── Responses API adapter ── */

void responses_api_init(void);
void responses_api_set_enabled(bool on);
bool responses_api_enabled(void);
cJSON *responses_api_build_request(const message_t *msgs, int n, const char *system_prompt,
                                   const char *model, const tool_def_t *tools, int nt,
                                   int max_tokens);
bool responses_api_is_response_format(const cJSON *body);
int responses_api_parse_output(const cJSON *body, message_t *out, int max_msgs);
const char *responses_api_finish_reason(const cJSON *body);
int responses_api_usage(const cJSON *body, int *input_tokens, int *output_tokens);
int responses_api_request_count(void);
int responses_api_error_count(void);
char *responses_api_format(void);

/* ── Team Memory Sync — cross-agent memory sharing with secret guard ── */

void team_memory_sync_init(void);
void team_memory_sync_free(void);
void team_memory_sync_set_enabled(bool on);
bool team_memory_sync_enabled(void);
int team_memory_add_secret_rule(const char *label, const char *pattern);
bool team_memory_scan_secrets(const char *text);
int team_memory_sync_put(const char *key, const char *value, const char *source_agent);
const char *team_memory_sync_get(const char *key);
int team_memory_sync_mark_synced(const char *key);
int team_memory_sync_unsynced_count(void);
int team_memory_sync_count(void);
int team_memory_sync_blocked_count(void);
int team_memory_sync_rule_count(void);
char *team_memory_sync_format(void);

/* ── Tool Call Runtime — unified tool call execution with tracking ── */

void tool_call_rt_init(void);
void tool_call_rt_free(void);
void tool_call_rt_reset(void);
int tool_call_rt_enqueue(const char *call_id, const char *tool_name, cJSON *arguments);
int tool_call_rt_execute(int idx, const tool_def_t *tools, int nt, void *ctx);
int tool_call_rt_cancel(int idx);
int tool_call_rt_count(void);
int tool_call_rt_pending_count(void);
int tool_call_rt_completed_total(void);
char *tool_call_rt_format(void);

/* Model Registry — static model metadata catalog (see model_registry.h) */
#include "model_registry.h"

/* Model Capabilities — per-model feature detection */
bool model_supports_thinking(const char *model);
bool model_supports_adaptive_thinking(const char *model);
const char *model_provider(const char *model);
bool model_supports_vision(const char *model);
bool model_supports_tools(const char *model);
bool model_supports_json_mode(const char *model);
bool model_uses_developer_role(const char *model);
bool model_uses_max_completion_tokens(const char *model);
int model_context_window(const char *model);
int model_max_output(const char *model);
int model_input_limit(const char *model);
char *model_caps_describe(const char *model);

/* Prompt Suggestion — next-prompt prediction engine */
typedef enum {
    FILTER_PASS = 0,
    FILTER_EMPTY = 1,
    FILTER_META = 2,
    FILTER_ERROR = 3,
    FILTER_PREFIX = 4,
    FILTER_TOO_SHORT = 5,
    FILTER_TOO_LONG = 6,
    FILTER_MULTI_SENTENCE = 7,
    FILTER_FORMATTING = 8,
    FILTER_EVALUATIVE = 9,
    FILTER_AI_VOICE = 10
} prompt_filter_t;

void prompt_suggest_init(bool enabled);
void prompt_suggest_set_enabled(bool enabled);
bool prompt_suggest_enabled(void);
void prompt_suggest_clear(void);
const char *prompt_suggest_current(void);
prompt_filter_t prompt_suggest_filter(const char *suggestion);
const char *prompt_filter_name(prompt_filter_t f);
char *prompt_suggest_generate(const void *conv, const char *base_url, const char *api_key,
                              const char *model, provider_type_t provider);
const char *prompt_suggest_prompt_text(void);
void prompt_suggest_free(void);

/* ── Plugin System ── */

typedef enum {
    PLUGIN_ON_STARTUP = 0,
    PLUGIN_ON_SHUTDOWN,
    PLUGIN_ON_TURN_START,
    PLUGIN_ON_TURN_END,
    PLUGIN_ON_TOOL_PRE,
    PLUGIN_ON_TOOL_POST,
    PLUGIN_ON_COMPACT,
    PLUGIN_ON_HANDOFF,
    PLUGIN_ON_ERROR,
    PLUGIN_ON_CUSTOM
} plugin_event_t;

typedef enum { PLUGIN_REGISTERED = 0, PLUGIN_LOADED, PLUGIN_UNLOADED, PLUGIN_ERROR } plugin_state_t;

typedef void *(*plugin_init_fn)(void);
typedef void (*plugin_unload_fn)(void *ctx);
typedef int (*plugin_hook_fn)(plugin_event_t event, const char *data, char **response, void *ctx);

typedef struct {
    char *name;
    char *path;
    char *version;
    char *description;
    void *dl_handle;
    void *ctx;
    plugin_init_fn init_fn;
    plugin_unload_fn unload_fn;
    plugin_hook_fn hook_fn;
    plugin_state_t state;
} plugin_entry_t;

typedef struct {
    plugin_entry_t plugins[MAX_PLUGINS];
    int count;
    bool enabled;
} plugin_registry_t;

void plugin_registry_init(plugin_registry_t *r);
void plugin_registry_free(plugin_registry_t *r);
plugin_registry_t *plugin_registry_global(void);
int plugin_register(plugin_registry_t *r, const char *name, plugin_init_fn init,
                    plugin_unload_fn unload, plugin_hook_fn hook);
int plugin_load(plugin_registry_t *r, const char *name);
int plugin_unload(plugin_registry_t *r, const char *name);
int plugin_load_so(plugin_registry_t *r, const char *path);
int plugin_load_dir(plugin_registry_t *r, const char *dir);
int plugin_dispatch(plugin_registry_t *r, plugin_event_t event, const char *data, char **response);
int plugin_count(const plugin_registry_t *r);
int plugin_loaded_count(const plugin_registry_t *r);
const char *plugin_state_name(plugin_state_t s);
const char *plugin_event_name(plugin_event_t ev);
char *plugin_format_list(const plugin_registry_t *r);
int plugin_discover_defaults(plugin_registry_t *r, const char *project_root);
int plugin_discover_home_plugin_count(void);
int plugin_file_loadable(const char *path);
bool plugin_project_plugins_allowed(void);
const char *plugin_market_plugin_sha256(int market_idx, int plugin_idx);

/* ── Memory Citations ── */

typedef struct {
    char *memory_key;
    char *session_id;
    int turn;
    char *source_text;
    long timestamp;
} mem_citation_entry_t;

typedef struct {
    mem_citation_entry_t entries[MAX_MEM_CITATIONS];
    int count;
} mem_citation_ctx_t;

void mem_citation_init(mem_citation_ctx_t *c);
void mem_citation_free(mem_citation_ctx_t *c);
mem_citation_ctx_t *mem_citation_global(void);
int mem_citation_add(mem_citation_ctx_t *c, const char *memory_key, const char *session_id,
                     int turn, const char *source_text);
int mem_citation_find(const mem_citation_ctx_t *c, const char *memory_key,
                      const mem_citation_entry_t **out, int max);
int mem_citation_by_session(const mem_citation_ctx_t *c, const char *session_id,
                            const mem_citation_entry_t **out, int max);
int mem_citation_remove(mem_citation_ctx_t *c, const char *memory_key);
int mem_citation_save(const mem_citation_ctx_t *c, const char *path);
int mem_citation_load(mem_citation_ctx_t *c, const char *path);
char *mem_citation_format(const mem_citation_ctx_t *c);
int mem_citation_count(const mem_citation_ctx_t *c);
int mem_citation_unique_sessions(const mem_citation_ctx_t *c);

/* ── Realtime Context ── */

void realtime_ctx_init(void);
void realtime_ctx_free(void);
void realtime_ctx_refresh(void);
const char *realtime_ctx_get(const char *key);
char *realtime_ctx_build_section(void);
bool realtime_ctx_stale(int max_age_s);
int realtime_ctx_probe_count(void);

/* ── Turn Metadata ── */

void turn_meta_init(void);
void turn_meta_free(void);
int turn_meta_begin(int turn);
void turn_meta_end(void);
void turn_meta_set_tokens(int input, int output);
void turn_meta_set_latency(long api_ms);
void turn_meta_record_tool(const char *name, long ms);
void turn_meta_record_file_read(void);
void turn_meta_record_file_write(int lines_added, int lines_removed);
void turn_meta_record_error(void);
void turn_meta_record_retry(void);
void turn_meta_mark_compacted(void);
int turn_meta_count(void);
int turn_meta_total_tools(void);
int turn_meta_total_errors(void);
long turn_meta_total_api_ms(void);
char *turn_meta_format(int turn_idx);
char *turn_meta_format_summary(void);

/* ── Thread Manager ── */

typedef enum {
    THREAD_ACTIVE = 0,
    THREAD_SUSPENDED = 1,
    THREAD_COMPLETED = 2,
    THREAD_ABORTED = 3
} nc_thread_state_t;

typedef struct {
    char *id;
    char *label;
    char *parent_id;
    char *model;
    char *last_task;
    nc_thread_state_t state;
    long created_at;
    long last_activity;
    int turn_count;
} thread_entry_t;

typedef struct {
    thread_entry_t threads[MAX_THREADS];
    int count;
    int active_idx;
} thread_mgr_t;

void thread_mgr_init(thread_mgr_t *t);
void thread_mgr_free(thread_mgr_t *t);
thread_mgr_t *thread_mgr_global(void);
int thread_mgr_create(thread_mgr_t *t, const char *label, const char *parent_id, const char *model);
int thread_mgr_find(const thread_mgr_t *t, const char *id);
int thread_mgr_suspend(thread_mgr_t *t, int idx);
int thread_mgr_resume(thread_mgr_t *t, int idx);
int thread_mgr_complete(thread_mgr_t *t, int idx);
int thread_mgr_abort(thread_mgr_t *t, int idx);
void thread_mgr_record_turn(thread_mgr_t *t, int idx);
int thread_mgr_active_count(const thread_mgr_t *t);
int thread_mgr_children(const thread_mgr_t *t, const char *parent_id, int *out, int max);
const char *thread_state_name(nc_thread_state_t s);
char *thread_mgr_format(const thread_mgr_t *t);

/* Conversation Export — Markdown / HTML / JSON */
typedef enum { EXPORT_MARKDOWN = 0, EXPORT_HTML = 1, EXPORT_JSON = 2 } conv_export_fmt_t;

struct conversation_s; /* forward decl — defined in conversation.h */
typedef struct conversation_s conv_fwd_t;

char *conv_export_markdown(const void *conv, const char *title);
char *conv_export_html(const void *conv, const char *title);
char *conv_export_json(const void *conv, const char *title);
int conv_export_to_file(const void *conv, const char *path, conv_export_fmt_t fmt,
                        const char *title);
conv_export_fmt_t conv_export_detect_format(const char *path);

/* Session Memory — auto-extract conversation notes for compaction survival */
void session_memory_init(void);
void session_memory_free(void);
void session_memory_set_enabled(bool enabled);
bool session_memory_enabled(void);
void session_memory_set_session(const char *session_id);
int session_memory_load(void);
int session_memory_save(void);
const char *session_memory_content(void);
int session_memory_extraction_count(void);
void session_memory_record_tool_call(void);
bool session_memory_should_extract(int current_turn);
char *session_memory_extract(const void *conv_ptr, const char *base_url, const char *api_key,
                             const char *model, provider_type_t provider);
char *session_memory_for_compact(void);
char *session_memory_for_prompt(void);
void session_memory_mark_extracted(int turn);

/* Background Consolidation — automatic memory review during idle */
void auto_dream_init(void);
void auto_dream_free(void);
void auto_dream_set_enabled(bool enabled);
bool auto_dream_enabled(void);
bool auto_dream_running(void);
int auto_dream_check(void);
void auto_dream_touch(void);
int auto_dream_total_runs(void);
int auto_dream_sessions_reviewed(void);
int auto_dream_files_touched(void);
char *auto_dream_status(void);

/* Trajectory Post-Compression — compress completed session trajectories */
int traj_compress_file(const char *input_path, const char *output_path);

/* Tool Batch Summary — concise labels for completed tool batches */
char *tool_batch_summarize(const char **names, const char **inputs, const char **outputs, int n);
char *tool_batch_summarize_single(const char *name, const char *input, const char *output);
char *tool_summary_truncate_json(const char *json, int max_len);

/* Post-Compact Cleanup — centralized cache/state reset after compaction */
void post_compact_init(void);
int post_compact_run(bool is_subagent);
int post_compact_total(void);
void post_compact_set_turn(int turn);
int post_compact_last_turn(void);
char *post_compact_describe(void);

/* Cache Hint — prompt cache optimization / break detection */
void cache_hint_init(void);
void cache_hint_set_enabled(bool enabled);
bool cache_hint_enabled(void);
int cache_hint_analyze(const char *system_prompt, const char *prev_system_prompt);
void cache_hint_record_hit(void);
void cache_hint_record_miss(void);
int cache_hint_hits(void);
int cache_hint_misses(void);
int cache_hint_breakpoints(const message_t *msgs, int n, int out_positions[], int max_out);
void cache_hint_set_tools_hash(const tool_def_t *tools, int num_tools);
unsigned long cache_hint_tools_hash(void);
void cache_hint_set_model_hash(const char *model);
void cache_hint_tick(void);
int cache_hint_tool_breaks(void);
int cache_hint_model_breaks(void);
int cache_hint_call_count(void);
char *cache_hint_format(void);

/* Shell Snapshot — capture shell state between tool calls */
void shell_snapshot_capture(void);
char *shell_snapshot_summary(void);
void shell_snapshot_free(void);

/* Agent Utilities — shared between agent.c and multi_agent.c */
char *agent_get_self_path(void);
int agent_build_exec_args(const void *cfg_ptr, const char *model, int max_turns, const char *prompt,
                          char *const **argv_out);

/* ── Time-based Pre-call MicroCompact ── */

void context_compact_touch_assistant(void);
time_t context_compact_last_assistant(void);
int context_compact_pre_call(void *conv, int gap_threshold_min, int keep_recent);

/* ── Agent Progress Summary ── */

void agent_summary_init(void);
void agent_summary_free(void);
int agent_summary_update(const char *agent_id, const char *summary);
int agent_summary_remove(const char *agent_id);
const char *agent_summary_get(const char *agent_id);
bool agent_summary_stale(const char *agent_id, int max_age_s);
int agent_summary_count(void);
char *agent_summary_extract_from_output(const char *output);
char *agent_summary_format_all(void);

/* ── Rollout Replay — session event reconstruction ── */

int rollout_replay_from_session(const char *session_id, message_t **out_msgs, int *out_count,
                                char **resume_ctx);
int rollout_replay_event_count(const char *session_id);
int rollout_replay_append_event(const char *session_id, const char *type, int turn,
                                const char *content, const cJSON *meta);
int rollout_replay_list_sessions(char **ids, int max);
char *rollout_replay_describe(const char *session_id);
void rollout_replay_free_msgs(message_t *msgs, int n);

/* Rollout truncation — prune old events to bound memory */
int rollout_truncate_find_user_boundaries(const cJSON *events_arr, int *positions, int max_pos);
int rollout_truncate_find_cut_point(const cJSON *events_arr, int keep_turns);
cJSON *rollout_truncate_events(const cJSON *events_arr, int keep_turns);
int rollout_truncate_file(const char *session_id, int keep_turns);
int rollout_truncate_check_needed(const char *session_id);

/* ── Agent Mailbox — inter-agent message passing ── */

typedef struct agent_mailbox agent_mailbox_t;

void agent_mailbox_init(agent_mailbox_t *mb, const char *owner_id);
void agent_mailbox_free(agent_mailbox_t *mb);
uint64_t agent_mailbox_send(agent_mailbox_t *mb, const char *from, const char *content);
int agent_mailbox_recv(agent_mailbox_t *mb, char **from_out, char **content_out);
int agent_mailbox_peek(const agent_mailbox_t *mb);
uint64_t agent_mailbox_seq(const agent_mailbox_t *mb);
int agent_mailbox_drain(agent_mailbox_t *mb, char **contents, int max);
char *agent_mailbox_format_status(const agent_mailbox_t *mb);

agent_mailbox_t *agent_mailbox_registry_create(const char *agent_id);
agent_mailbox_t *agent_mailbox_registry_find(const char *agent_id);
int agent_mailbox_registry_remove(const char *agent_id);
void agent_mailbox_registry_clear(void);
int agent_mailbox_registry_count(void);

/* ── Agent Roles — built-in and custom role system ── */

void agent_roles_init(void);
void agent_roles_free(void);
int agent_roles_load_dir(const char *dir);
int agent_role_count(void);
const char *agent_role_name(int idx);
const char *agent_role_display(int idx);
const char *agent_role_prompt(const char *name);
bool agent_role_is_readonly(const char *name);
bool agent_role_tool_allowed(const char *role_name, const char *tool_name);
char *agent_roles_format_list(void);
void agent_role_apply_to_spawn(const char *role_name, agent_spawn_opts_t *opts,
                               char **system_prompt_out, char **model_out);

/* ── Model Discovery — dynamic API model catalog ── */

int model_discovery_fetch(const char *base_url, const char *api_key);
bool model_discovery_cache_valid(void);
int model_discovery_count(void);
const char *model_discovery_id(int idx);
const char *model_discovery_name(int idx);
bool model_discovery_exists(const char *model_id);
int model_discovery_context_window(const char *model_id);
void model_discovery_clear(void);
char *model_discovery_format(void);

/* ── Task Scheduler — background cron-style execution ── */

void scheduler_init(void);
int scheduler_add(const char *label, const char *command, int delay_s, int interval_s, int max_runs,
                  char *id_out, size_t id_sz);
int scheduler_cancel(const char *task_id);
int scheduler_active_count(void);
const char *scheduler_task_output(const char *task_id);
char *scheduler_format_list(void);
int cron_tool_exec(const cJSON *input, char **output, void *ctx);

/* ── Risk Assessment Engine ── */

typedef enum { RISK_LOW = 0, RISK_MEDIUM = 1, RISK_HIGH = 2, RISK_CRITICAL = 3 } risk_level_t;

#define RISK_THRESHOLD_MEDIUM 25
#define RISK_THRESHOLD_HIGH 60

risk_level_t risk_assess_tool(const char *tool_name, const cJSON *input);
const char *risk_level_name(risk_level_t level);
int risk_score_command(const char *cmd);
int risk_score_path(const char *path);
char *risk_assess_describe(const char *tool_name, const cJSON *input);

typedef enum {
    RISK_OUTCOME_ALLOW = 0,
    RISK_OUTCOME_REVIEW = 1,
    RISK_OUTCOME_DENY = 2
} risk_outcome_t;

typedef struct {
    char message[TERR_MSG_MAX];
    char reason[TERR_MSG_MAX];
} risk_evidence_t;

typedef struct {
    risk_level_t level;
    int score;
    char rationale[TERR_DETAIL_MAX];
    risk_evidence_t evidence[GRD_MAX_EVIDENCE_ITEMS];
    int evidence_count;
} risk_assessment_t;

void risk_assess_init(risk_assessment_t *a);
int risk_assess_action(risk_assessment_t *a, const char *tool_name, const char *action_text);
risk_outcome_t risk_assess_outcome(const risk_assessment_t *a);
char *risk_assess_format(const risk_assessment_t *a);
const char *risk_outcome_name(risk_outcome_t o);

/* ── Session / Turn State Machine ── */

typedef enum {
    SESSION_IDLE = 0,
    SESSION_INITIALIZING,
    SESSION_READY,
    SESSION_RUNNING,
    SESSION_COMPACTING,
    SESSION_SUSPENDED,
    SESSION_SHUTTING_DOWN,
    SESSION_TERMINATED
} session_phase_t;

typedef enum {
    TURN_IDLE = 0,
    TURN_PREPARING,
    TURN_API_CALL,
    TURN_STREAMING,
    TURN_TOOL_ROUTING,
    TURN_TOOL_EXECUTING,
    TURN_RESULT_COLLECTING,
    TURN_POST_PROCESSING,
    TURN_COMPLETE
} turn_phase_t;

#define MAX_STATE_CALLBACKS 16

typedef void (*state_transition_cb_t)(session_phase_t from, session_phase_t to, void *ctx);

void state_machine_init(const char *session_id);
void state_machine_free(void);
int state_add_transition_cb(state_transition_cb_t cb, void *ctx);
int state_session_begin(void);
int state_session_ready(void);
int state_session_run(void);
int state_session_compact(void);
int state_session_suspend(void);
int state_session_resume(void);
int state_session_shutdown(void);
int state_session_terminate(void);
session_phase_t state_session_phase(void);
const char *state_session_phase_name(session_phase_t p);
int state_session_turn_number(void);
const char *state_session_id(void);
long state_session_uptime_ms(void);
long state_session_idle_ms(void);
int state_turn_begin(void);
int state_turn_api_call(void);
int state_turn_streaming(void);
int state_turn_tool_route(void);
int state_turn_tool_exec(void);
int state_turn_result_collect(void);
int state_turn_post_process(void);
int state_turn_complete(void);
const char *state_turn_phase_name(turn_phase_t p);
int state_turn_id(void);
int state_turn_tool_count(void);
char *state_format_status(void);

/* ── Event Bus ── */

typedef enum {
    EVT_SESSION_START = 0,
    EVT_SESSION_END,
    EVT_SESSION_SUSPEND,
    EVT_SESSION_RESUME,
    EVT_TURN_START,
    EVT_TURN_END,
    EVT_API_CALL_START,
    EVT_API_CALL_END,
    EVT_API_ERROR,
    EVT_TOOL_CALL_START,
    EVT_TOOL_CALL_END,
    EVT_TOOL_ERROR,
    EVT_COMPACT_START,
    EVT_COMPACT_END,
    EVT_MEMORY_LOAD,
    EVT_MEMORY_SAVE,
    EVT_CONFIG_RELOAD,
    EVT_HOOK_FIRE,
    EVT_AGENT_SPAWN,
    EVT_AGENT_COMPLETE,
    EVT_AGENT_ERROR,
    EVT_FILE_READ,
    EVT_FILE_WRITE,
    EVT_FILE_DELETE,
    EVT_RISK_ASSESSED,
    EVT_PERMISSION_GRANT,
    EVT_PERMISSION_DENY,
    EVT_CUSTOM
} event_type_t;

typedef struct {
    event_type_t type;
    const char *tool_name;
    const char *detail;
    int score;
    long timestamp;
} event_t;

#define MAX_EVENT_SUBS 64

typedef void (*event_handler_fn)(const event_t *event, void *ctx);

void event_bus_init(void);
void event_bus_free(void);
int event_bus_subscribe(event_type_t type, event_handler_fn handler, void *ctx);
int event_bus_unsubscribe(int sub_id);
int event_bus_emit(const event_t *event);
int event_bus_emit_simple(event_type_t type, const char *detail);
int event_bus_emit_tool(event_type_t type, const char *tool_name, const char *detail, int score);
char *event_bus_format_stats(void);

/* ── Config Loader — multi-layer configuration ── */

void config_loader_init(void);
void config_loader_free(void);
int config_loader_load_all(const char *project_root);
cJSON *config_loader_merged(void);
int config_loader_get_int(const char *key, int fallback);
int config_loader_reload_if_stale(const char *project_root);
char *config_loader_format(void);

/* ── Skills Watcher — live skill file monitoring ── */

#define MAX_TRACKED_SKILLS 64

void skills_watcher_init(void);
void skills_watcher_free(void);
int skills_watcher_add_dir(const char *dir);
bool skills_watcher_check(void);
int skills_watcher_count(void);
char *skills_watcher_format(void);

/* ── Async Process Watcher ── */

#define MAX_ASYNC_WATCHED 32

void async_watcher_init(void);
void async_watcher_free(void);
int async_watcher_add(pid_t pid, int stdout_fd, int stderr_fd, long timeout_ms, const char *label);
int async_watcher_poll(void);
bool async_watcher_done(int idx);
int async_watcher_exit_code(int idx);
char *async_watcher_output(int idx);
int async_watcher_active_count(void);

/* ── Code Mode — specialized code editing ── */

void code_mode_init(void);
int code_mode_enter(const char *file, const char *language);
void code_mode_exit(void);
bool code_mode_active(void);
const char *code_mode_language(void);
const char *code_mode_detect_intent(const char *text);
char *code_mode_build_prompt(void);
const char *code_mode_detect_language(const char *path);

/* ── IDE Connector — JSON-RPC over stdio bridge ── */

#define CONNECTOR_METHOD_MAX 64
#define CONNECTOR_MAX_HANDLERS 32

typedef int (*connector_handler_fn)(const cJSON *params, cJSON **result, void *ctx);

void connector_init(void);
void connector_free(void);
int connector_register(const char *method, connector_handler_fn fn, void *ctx);
int connector_dispatch(const char *json_line, char **response);
char *connector_format_methods(void);

/* ── Parallel Tool Execution ── */

void pexec_init(int max_concurrent, long timeout_ms, void *tool_ctx);
void pexec_free(void);
int pexec_enqueue(const tool_def_t *tool, cJSON *input);
int pexec_launch_all(void);
int pexec_wait_all(void);
int pexec_done_count(void);
bool pexec_all_done(void);
const char *pexec_slot_output(int idx);
int pexec_slot_rc(int idx);

/* ── File Index ── */

void file_index_init(void);
void file_index_free(void);
int file_index_build(const char *root_dir);
bool file_index_ready(void);
int file_index_count(void);
long file_index_age_s(void);
const char *file_index_root(void);
bool file_index_lookup(const char *rel_path);
int file_index_glob(const char *pattern, const char **out, int max);
int file_index_fuzzy(const char *query, fuzzy_result_t *out, int max);
int file_index_prefix(const char *prefix, const char **out, int max);
char *file_index_format_stats(void);

/* ── LSP Bridge ── */

typedef enum {
    LSP_DISCONNECTED = 0,
    LSP_STARTING,
    LSP_READY,
    LSP_SHUTTING_DOWN,
    LSP_ERROR
} lsp_state_t;

void lsp_bridge_init(void);
void lsp_bridge_free(void);
int lsp_bridge_start(const char *language, const char *command, const char *root_uri);
int lsp_bridge_stop(int idx);
int lsp_bridge_count(void);
const char *lsp_state_name(lsp_state_t s);
char *lsp_bridge_format(void);

char *lsp_bridge_execute(const char *language, const char *operation, const char *file_path,
                         int line, int character, const char *query);
int lsp_bridge_did_open(const char *language, const char *file_path, const char *text,
                        const char *lang_id);

/* ── Tool Call Parsers ──
 *
 * When the assistant message body carries tool invocations as tagged or
 * delimited text instead of a native tool_calls array, these parsers recover
 * structured name/arguments pairs for the agent loop.
 */

typedef struct {
    char name[NC_TAG_MAX];
    char arguments[TC_PARSER_ID_MAX * 1024];
    char id[TC_PARSER_ID_MAX];
} parsed_tool_call_t;

typedef struct {
    char content[TC_PARSER_ID_MAX * 1024]; /* text before tool calls */
    parsed_tool_call_t calls[TC_MAX_CALLS_PER_PARSE];
    int n_calls;
} tc_parse_result_t;

typedef int (*tc_parse_fn)(const char *text, tc_parse_result_t *out);

void tc_parser_init(void);
int tc_parser_register(const char *id, tc_parse_fn fn);
int tc_parser_parse(const char *parser_id, const char *text, tc_parse_result_t *out);
int tc_parser_auto(const char *text, tc_parse_result_t *out);
int tc_parser_count(void);
const char *tc_parser_list(void);

/* ── Swarm Mode ── */

typedef enum {
    SWARM_IDLE = 0,
    SWARM_ACTIVE,
    SWARM_CONVERGING,
    SWARM_COMPLETE,
    SWARM_ABORTED
} swarm_phase_t;

void swarm_init(int max_agents);
void swarm_free(void);
int swarm_begin(const char *goal, long deadline_s);
int swarm_add_work(const char *description, int priority);
int swarm_assign_next(const char *agent_id);
int swarm_complete_work(int idx, const char *result, bool success);
void swarm_end(void);
swarm_phase_t swarm_phase(void);
int swarm_pending_count(void);
int swarm_completed_count(void);
int swarm_failed_count(void);
bool swarm_timed_out(void);
const char *swarm_phase_name(swarm_phase_t p);
char *swarm_format_status(void);

/* ── Hardened Execution ── */

typedef struct {
    long cpu_time_limit_s;
    size_t mem_limit_bytes;
    size_t output_limit_bytes;
    int nice_value;
    bool new_session;
} hardened_limits_t;

typedef struct {
    int exit_code;
    int signal_num;
    bool timed_out;
    bool oom_killed;
    long wall_ms;
    long user_ms;
    long sys_ms;
    size_t output_bytes;
    bool output_truncated;
} hardened_result_t;

int hardened_exec(const char *cmd, const hardened_limits_t *limits, char **output,
                  hardened_result_t *result);
int hardened_exec_default(const char *cmd, char **output, hardened_result_t *result);
int hardened_exec_quick(const char *cmd, int timeout_s, char **output, hardened_result_t *result);
void hardened_limits_default(hardened_limits_t *l);
char *hardened_format_stats(void);

/* ── Secrets Detection & Redaction ── */

void secrets_init(void);
void secrets_free(void);
int secrets_allow(const char *value);
int secrets_scan(const char *text, int *positions, int *lengths, const char **labels, int max);
bool secrets_contains(const char *text);
char *secrets_redact(const char *text);
int secrets_count_patterns(void);

/* ── State DB — persistent session state layer ── */

void state_db_init(void);
void state_db_free(void);
bool state_db_ready(void);
int state_db_save_session(const char *session_id, const cJSON *data);
cJSON *state_db_load_session(const char *session_id);
bool state_db_session_exists(const char *session_id);
int state_db_delete_session(const char *session_id);
int state_db_list_sessions(char **ids, int max);
int state_db_save_rollout(const char *session_id, int turn, const cJSON *data);
cJSON *state_db_load_rollout(const char *session_id, int turn);
int state_db_count_rollouts(const char *session_id);
bool state_db_try_lock(const char *name, int ttl_s);
void state_db_unlock(const char *name);
int state_db_sessions_count(void);
int state_db_total_writes(void);
char *state_db_format_stats(void);

/* ── Memory Pipeline — 2-phase extraction + consolidation ── */

void mem_pipeline_init(void);
void mem_pipeline_free(void);
bool mem_pipeline_ready(void);
int mem_pipeline_phase1_extract(const char *session_id, const message_t *msgs, int n_msgs,
                                char **raw_memory_out, char **summary_out);
int mem_pipeline_phase2_consolidate(int max_memories);
int mem_pipeline_phase1_count(void);
char *mem_pipeline_format_status(void);

/* ── Server Mode — JSON-RPC server over localhost TCP ── */

bool server_mode_active(void);
int server_mode_port(void);
int server_mode_total_requests(void);
char *server_mode_format_status(void);
int server_mode_start(int port);
void server_mode_stop(void);
int server_mode_poll(int timeout_ms);
void server_mode_broadcast(const char *json_notification);

/* ── Project instruction filenames (relative paths) — project_doc_paths.c ── */

extern const char *nc_project_doc_leaf_names[];
extern const int nc_project_doc_leaf_n;
extern const char *nc_project_doc_agents_paths[];
extern const int nc_project_doc_agents_n;
extern const char *nc_project_doc_extra_paths[];
extern const int nc_project_doc_extra_n;
bool nc_project_doc_is_filename(const char *base);

/* ── Plugin Manifest — manifest-based plugin discovery ── */

void plugin_manifest_init(void);
void plugin_manifest_free(void);
int plugin_manifest_load_file(const char *path);
int plugin_manifest_discover(const char *dir);
int plugin_manifest_count(void);
int plugin_manifest_find(const char *id);
char *plugin_manifest_format_list(void);

/* ── Task Type System — formal task lifecycle ── */

typedef enum {
    TASK_KIND_REGULAR = 0,
    TASK_KIND_COMPACT,
    TASK_KIND_REVIEW,
    TASK_KIND_UNDO,
    TASK_KIND_USER_SHELL,
    TASK_KIND_GHOST_SNAPSHOT,
    TASK_KIND_MEMORY,
    TASK_KIND_AGENT
} task_kind_t;

void task_types_init(void);
void task_types_free(void);
int task_types_create(task_kind_t kind, const char *label, const char *session_id);
int task_types_start(int task_id);
int task_types_finish(int task_id, int exit_code, const char *detail);
int task_types_abort(int task_id);
bool task_types_running(int task_id);
bool task_types_done(int task_id);
long task_types_elapsed_ms(int task_id);
int task_types_count(void);
int task_types_active_count(void);
int task_types_by_kind(task_kind_t kind, int *ids, int max);
const char *task_kind_name(task_kind_t kind);
char *task_types_format(void);

/* ── Tool Orchestration — unified approval→sandbox→exec→retry pipeline ── */

typedef enum {
    ORCH_OK = 0,
    ORCH_DENIED,
    ORCH_TIMEOUT,
    ORCH_ERROR,
    ORCH_RETRY_EXHAUSTED
} orch_result_t;

void orch_init(void);
void orch_free(void);
orch_result_t orch_execute(const tool_def_t *tool, cJSON *input, char **output, void *ctx);
int orch_execute_batch(const tool_def_t **tools, cJSON **inputs, char ***outputs, int count,
                       void *ctx);
int orch_total_calls(void);
int orch_total_successes(void);
int orch_total_empty_results(void);
const char *orch_result_name(orch_result_t r);
char *orch_format_stats(void);

/* ── Session Transcript — persistent event log ── */

void transcript_init(void);
void transcript_free(void);
int transcript_append(const char *event_type, const char *content);
int transcript_entry_count(void);
char *transcript_entry_at(int idx);
char *transcript_format_recent(int max);

/* ── Environment Context — dynamic environment probing ── */

void env_ctx_init(void);
void env_ctx_free(void);
void env_ctx_set(const char *key, const char *value);
const char *env_ctx_get(const char *key);
int env_ctx_probe_system(void);
char *env_ctx_build_prompt_section(void);
int env_ctx_count(void);
char *env_ctx_format(void);

/* ── Team Manager — agent team orchestration ── */

void team_mgr_init(void);
void team_mgr_free(void);
int team_create(const char *name, const char *goal);
int team_add_member(int team_id, const char *agent_id, const char *role);
int team_remove_member(int team_id, const char *agent_id);
int team_disband(int team_id);
int team_count(void);
int team_member_count(int team_id);
const char *team_name(int team_id);
const char *team_goal(int team_id);
char *team_format_status(int team_id);
char *team_format_all(void);

/* ── Auto-Compact — intelligent compaction trigger ── */

void auto_compact_init(int model_ctx_window);
void auto_compact_free(void);
void auto_compact_set_enabled(bool on);
bool auto_compact_enabled(void);
int auto_compact_effective_window(void);
int auto_compact_total(void);
void auto_compact_set_window(int ctx_win, int output_reserve);
int auto_compact_usage_pct(int current_tokens);
bool auto_compact_should_trigger(int current_tokens);
int auto_compact_run(void *conv_ptr, int current_tokens, const char *session_id);
void auto_compact_record_failure(void);
void auto_compact_reset_circuit(void);
int auto_compact_circuit_trips(void);
char *auto_compact_format(void);
void auto_compact_set_input_limit(int lim);
bool auto_compact_is_overflow(int token_count, int output_tokens);
bool auto_compact_should_warn_degradation(void);
int auto_compact_full_count(void);

/* ── Context Reference Tracker ── */

void ctx_ref_init(void);
void ctx_ref_set_turn(int turn);
void ctx_ref_track(const char *path);
void ctx_ref_pin(const char *path);
bool ctx_ref_is_active(const char *path, int recency_turns);
int ctx_ref_count(void);
int ctx_ref_active_count(int recency_turns);
char *ctx_ref_format_active(int recency_turns);
void ctx_ref_clear(void);

/* ── Living Documentation — self-updating project docs ── */

void magic_docs_init(void);
void magic_docs_free(void);
void magic_docs_set_enabled(bool on);
bool magic_docs_enabled(void);
bool magic_docs_detect(const char *path, const char *content);
int magic_docs_register(const char *path, const char *content);
void magic_docs_record_update(int idx);
int magic_docs_count(void);
int magic_docs_updates(void);
const char *magic_docs_title(int idx);
char *magic_docs_format(void);

/* ── Resource Monitor — local system resource tracking ── */

void resource_monitor_init(void);
void resource_monitor_free(void);
int resource_monitor_poll(void);
bool resource_monitor_low_memory(int threshold_pct);
bool resource_monitor_low_disk(long min_free_mb);
double resource_monitor_avg_cpu(void);
int resource_monitor_sample_count(void);
char *resource_monitor_format(void);

/* ── Query Mode — one-shot query execution ── */

typedef void (*query_stop_hook_fn)(void *ctx);

void query_mode_init(const char *prompt, int budget);
void query_mode_free(void);
bool query_mode_active(void);
void query_mode_set_max_tools(int max);
int query_mode_add_stop_hook(query_stop_hook_fn fn, void *ctx);
bool query_mode_consume(int tokens);
void query_mode_record_tool(void);
bool query_mode_should_stop(void);
void query_mode_stop(void);
int query_mode_budget(void);
bool query_mode_over_budget(void);
long query_mode_elapsed_ms(void);
int query_mode_remaining(void);
char *query_mode_format(void);

/* ── Session Prewarm — pre-warm API connection ── */

void session_prewarm_init(void);
void session_prewarm_free(void);
int session_prewarm_start(const char *base_url, const char *api_key, provider_type_t provider,
                          long timeout_ms);
void session_prewarm_cancel(void);
bool session_prewarm_ready(void);
void *session_prewarm_consume(void);
long session_prewarm_latency_ms(void);
long session_prewarm_age_ms(void);
const char *session_prewarm_status_name(void);
char *session_prewarm_format(void);

/* ── Startup warmup — session boot readiness (config, workspace, tools, docs) ── */

typedef enum {
    WARMUP_IDLE = 0,
    WARMUP_RUNNING = 1,
    WARMUP_READY = 2,
    WARMUP_TIMEOUT = 3,
    WARMUP_FAILED = 4,
    WARMUP_CANCELLED = 5
} warmup_status_t;

void warmup_init(long timeout_ms);
void warmup_free(void);
void warmup_start(void);
void warmup_mark_api_verified(void);
void warmup_mark_config_loaded(void);
void warmup_mark_workspace_scanned(void);
void warmup_mark_project_docs(void);
void warmup_set_tools(int count);
void warmup_complete(void);
void warmup_cancel(void);
void warmup_fail(void);
bool warmup_check_timeout(void);
bool warmup_is_ready(void);
warmup_status_t warmup_status(void);
long warmup_elapsed_ms(void);
bool warmup_has_project_docs(void);
int warmup_tool_count(void);
const char *warmup_status_name(warmup_status_t s);
char *warmup_format(void);

/* ── Event Dispatch — unified session event routing ── */

typedef enum {
    OUTPUT_HUMAN = 0,
    OUTPUT_JSONL = 1,
    OUTPUT_QUIET = 2,
    OUTPUT_CALLBACK = 3
} output_mode_t;

typedef enum {
    EV_TURN_START = 0,
    EV_TURN_COMPLETE = 1,
    EV_TURN_ABORT = 2,
    EV_TEXT_DELTA = 3,
    EV_TOOL_START = 4,
    EV_TOOL_DONE = 5,
    EV_COMPACT = 6,
    EV_ERROR = 7,
    EV_WARNING = 8,
    EV_SESSION_START = 9,
    EV_SESSION_END = 10,
    EV_COST_UPDATE = 11,
    EV_HANDOFF = 12,
    EV_CUSTOM = 13
} dispatch_event_kind_t;

typedef struct {
    dispatch_event_kind_t kind;
    int turn;
    const char *tool_name;
    const char *text;
    int tokens_in;
    int tokens_out;
    double cost;
    long timestamp;
} dispatch_event_t;

typedef void (*dispatch_handler_fn)(const dispatch_event_t *event, void *ctx);

void event_dispatch_init(output_mode_t mode);
void event_dispatch_free(void);
int event_dispatch_subscribe(dispatch_handler_fn fn, void *ctx, int event_mask);
void event_dispatch_emit(const dispatch_event_t *ev);
void event_dispatch_text(int turn, const char *text);
void event_dispatch_tool(int turn, const char *tool, bool done);
void event_dispatch_turn_complete(int turn, int tok_in, int tok_out, double cost);
void event_dispatch_error(int turn, const char *msg);
output_mode_t event_dispatch_mode(void);
long event_dispatch_count(void);
const char *output_mode_name(output_mode_t m);

/* ── Plugin hooks — dynamic tool/hook registration ── */

typedef enum {
    INJECT_TOOL = 0,
    INJECT_HOOK = 1,
    INJECT_CONTEXT = 2,
    INJECT_SKILL = 3
} inject_kind_t;

void plugin_inject_init(void);
void plugin_inject_free(void);
int plugin_inject_register(const char *plugin_id, inject_kind_t kind, const char *name,
                           const char *desc, void *handle);
int plugin_inject_count(void);
int plugin_inject_count_by_kind(inject_kind_t kind);
void *plugin_inject_find(const char *name, inject_kind_t kind);
int plugin_inject_remove(const char *plugin_id, const char *name, inject_kind_t kind);
int plugin_inject_remove_all(const char *plugin_id);
char *plugin_inject_format(void);

/* ── Session Index — cross-session tracking ── */

void session_index_init(void);
void session_index_free(void);
int session_index_add(const char *session_id, const char *workspace, const char *model);
int session_index_update_activity(const char *session_id, int turns, double cost);
int session_index_set_archived(const char *session_id, bool archived);
int session_index_set_memories(const char *session_id, bool has_memories);
int session_index_count(void);
int session_index_active_count(void);
char *session_index_serialize(void);
int session_index_deserialize(const char *json);
char *session_index_format(void);

/* ── Memory Phases — two-phase extraction pipeline ── */

typedef enum {
    MEM_PHASE_RAW = 0,
    MEM_PHASE_EXTRACTED = 1,
    MEM_PHASE_MERGED = 2,
    MEM_PHASE_ARCHIVED = 3
} mem_phase_t;

void mem_phase_init(void);
void mem_phase_free(void);
int mem_phase1_add_raw(const char *content, const char *session_id, int tokens);
int mem_phase1_extract(int idx);
int mem_phase1_run_all(void);
int mem_phase2_consolidate(void);
int mem_phase_count(void);
int mem_phase_raw_count(void);
int mem_phase_extracted_count(void);
bool mem_phase_is_running(void);
int mem_phase_active_count(void);
char *mem_phase_format_status(void);

/* ── Interrupted Turn — abort tracking & history markers ── */

typedef enum {
    INTR_USER_CANCEL = 0,
    INTR_TIMEOUT = 1,
    INTR_SYSTEM = 2,
    INTR_COMPACT = 3
} interrupt_reason_t;

void interrupt_ctx_init(void);
void interrupt_ctx_free(void);
int interrupt_record(int turn, interrupt_reason_t reason, int pending_tools, bool partial_output);
char *interrupt_build_marker(int turn, interrupt_reason_t reason);
int interrupt_total_count(void);
bool interrupt_was_recent(int turns_ago);
const char *interrupt_reason_name(interrupt_reason_t r);
char *interrupt_format_history(void);

/* ── Auto-fix — issue detection & automated remediation ── */

typedef enum {
    FIX_LINT = 0,
    FIX_FORMAT = 1,
    FIX_IMPORT = 2,
    FIX_TYPECHECK = 3,
    FIX_BUILD = 4,
    FIX_TEST = 5
} fix_category_t;

typedef enum {
    FIX_STATUS_PENDING = 0,
    FIX_STATUS_APPLIED = 1,
    FIX_STATUS_FAILED = 2,
    FIX_STATUS_SKIPPED = 3
} fix_status_t;

void autofix_init(void);
void autofix_free(void);
int autofix_add_issue(const char *file, int line, const char *message);
int autofix_apply(int idx);
int autofix_count(void);
int autofix_pending_count(void);
const char *fix_category_name(fix_category_t c);
const char *fix_status_name(fix_status_t s);
char *autofix_format(void);

/* ── Context Visualization — context window breakdown ── */

typedef struct {
    const char *label;
    int tokens;
    int pct;
} ctx_segment_t;

typedef struct {
    ctx_segment_t segments[16];
    int count;
    int total_tokens;
    int window_size;
    int free_tokens;
} ctx_viz_result_t;

int ctx_viz_analyze(ctx_viz_result_t *viz, const message_t *msgs, int msg_count, int context_window,
                    int system_prompt_tokens);
char *ctx_viz_render(const ctx_viz_result_t *viz);
char *ctx_viz_render_json(const ctx_viz_result_t *viz);

/* ── Event Mapping — response to turn item classification ── */

typedef enum {
    TURN_ITEM_NONE = 0,
    TURN_ITEM_USER_MSG,
    TURN_ITEM_AGENT_MSG,
    TURN_ITEM_REASONING,
    TURN_ITEM_TOOL_CALL,
    TURN_ITEM_TOOL_RESULT,
    TURN_ITEM_WEB_SEARCH,
    TURN_ITEM_IMAGE_GEN,
    TURN_ITEM_HOOK_PROMPT
} turn_item_kind_t;

typedef struct {
    turn_item_kind_t kind;
    char *id;
    char *text;
    char *tool_name;
    char *phase;
    int turn_id;
} turn_item_t;

int event_map_message(const message_t *m, int turn_id, turn_item_t *out);
int event_map_conversation(const message_t *msgs, int n, turn_item_t *items, int max_items);
bool event_map_is_contextual_text(const char *text);
bool event_map_is_user_boundary(const message_t *m);
const char *turn_item_kind_name(turn_item_kind_t k);
char *event_map_format_items(const turn_item_t *items, int n);

/* ── Contextual Message Fragments ── */

typedef enum {
    CTX_FRAG_NONE = 0,
    CTX_FRAG_ENV,
    CTX_FRAG_INSTRUCTIONS,
    CTX_FRAG_SKILL,
    CTX_FRAG_SHELL_CMD,
    CTX_FRAG_TURN_ABORT,
    CTX_FRAG_SUBAGENT,
    CTX_FRAG_MODEL_SWITCH,
    CTX_FRAG_PERSONALITY,
    CTX_FRAG_HOOK_PROMPT
} ctx_frag_kind_t;

ctx_frag_kind_t ctx_frag_classify(const char *text);
bool ctx_frag_is_contextual(const char *text);
bool ctx_frag_exclude_memory(const char *text);
bool ctx_msg_is_contextual(const message_t *m);
bool ctx_msg_is_user_boundary(const message_t *m);
int ctx_msg_filter_content(const message_t *m, strbuf_t *out);
const char *ctx_frag_kind_name(ctx_frag_kind_t k);
int ctx_frag_count_in_conversation(const message_t *msgs, int n);
int ctx_frag_user_turn_count(const message_t *msgs, int n);

/* ── Fork Backend — optimized process spawning ── */

typedef enum {
    FORK_STDIO_PIPE = 0,
    FORK_STDIO_PIPE_MERGED,
    FORK_STDIO_INHERIT,
    FORK_STDIO_NULL
} fork_stdio_t;

typedef struct {
    const char *program;
    const char **args;
    int argc;
    const char *cwd;
    const char *arg0;
    fork_stdio_t stdio_mode;
    bool detach_tty;
    bool parent_death_sig;
    bool kill_on_parent_exit;
    bool new_session;
} fork_request_t;

typedef struct {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    int exit_code;
    bool exited;
    bool signaled;
    int term_signal;
} fork_child_t;

int fork_spawn(const fork_request_t *req, fork_child_t *child);
int fork_wait(fork_child_t *child, int timeout_ms);
int fork_kill(fork_child_t *child, int sig);
int fork_read_output(fork_child_t *child, char **out, size_t max_bytes);
void fork_child_cleanup(fork_child_t *child);
int fork_exec_simple(const char *cmd, const char *cwd, int timeout_ms, char **output,
                     int *exit_code);

/* ── Rollout Reconstruction — enhanced resume/fork ── */

int rollout_replay_reconstruct(const char *session_id, message_t **out_msgs, int *out_count,
                               char **resume_ctx, char **reference_ctx);
int rollout_replay_drop_last_n_turns(const char *session_id, int n);

/* ── Code Mode Execute/Wait/Notify ── */

typedef enum {
    CM_EXEC_IDLE = 0,
    CM_EXEC_RUNNING,
    CM_EXEC_YIELDED,
    CM_EXEC_COMPLETED,
    CM_EXEC_FAILED
} cm_exec_status_t;

void code_mode_exec_reset(void);
int code_mode_exec_start(const char *cell_id, const char *script);
int code_mode_exec_yield(const char *partial_output);
int code_mode_exec_resume(void);
int code_mode_exec_complete(const char *output, const char *error);
int code_mode_exec_notify(const char *text);
void code_mode_exec_record_tool_call(void);
cm_exec_status_t code_mode_exec_status(void);
const char *code_mode_exec_output(void);
long code_mode_exec_wall_ms(void);
const char *code_mode_exec_status_name(cm_exec_status_t s);
char *code_mode_exec_format_result(void);

/* ── Tool Router — central tool call routing ── */

typedef enum {
    PAYLOAD_FUNCTION = 0,
    PAYLOAD_LOCAL_SHELL,
    PAYLOAD_MCP,
    PAYLOAD_CUSTOM,
    PAYLOAD_SEARCH,
    PAYLOAD_UNKNOWN,
    PAYLOAD_KIND_COUNT
} payload_kind_t;

typedef struct {
    char *tool_name;
    char *call_id;
    char *namespace;
    payload_kind_t kind;
    cJSON *arguments;
} routed_call_t;

void tool_router_init(void);
void tool_router_free(void);
int tool_router_add_mcp_prefix(const char *prefix);
int tool_router_classify(const char *name, const cJSON *item, routed_call_t *out);
int tool_router_total(void);
const char *payload_kind_name(payload_kind_t k);
char *tool_router_format_stats(void);
void routed_call_init(routed_call_t *rc);
void routed_call_free(routed_call_t *rc);

/* ── Sub-Agent Delegate — event-forwarding delegation ── */

typedef enum {
    DELEG_IDLE = 0,
    DELEG_RUNNING,
    DELEG_COMPLETING,
    DELEG_CANCELLED,
    DELEG_DONE
} delegate_phase_t;

void delegate_init(void);
void delegate_free(void);
char *delegate_spawn_interactive(const char *parent_id, const char *model, int max_turns,
                                 const char *system_prompt);
char *delegate_spawn_one_shot(const char *parent_id, const char *model, const char *prompt);
int delegate_send_input(const char *id, const char *input);
int delegate_cancel(const char *id);
int delegate_wait(const char *id, int timeout_ms, char **result_out);
delegate_phase_t delegate_phase(const char *id);
int delegate_count(void);
int delegate_active_count(void);
const char *delegate_phase_name(delegate_phase_t p);
char *delegate_format_status(void);

/* ── Memory Trace — session trace to memory conversion ── */

typedef struct {
    char *memory_id;
    char *source_path;
    char *raw_text;
    char *summary;
} built_memory_t;

int mem_trace_build(const char **paths, int count, built_memory_t *out, int max_out);
void mem_trace_free_memories(built_memory_t *mems, int count);
char *mem_trace_format(const built_memory_t *mems, int count);

/* ── Memory Directory — structured file-based memory scanning ── */

typedef struct {
    char *filename;
    char *filepath;
    long mtime;
    char *description;
    int type;
} memdir_header_t;

void memdir_init(void);
void memdir_free(void);
int memdir_scan(const char *dir, memdir_header_t *out, int max);
char *memdir_format_listing(const memdir_header_t *hdrs, int count);
int memdir_read_entrypoint(const char *dir, char **content_out);
int memdir_write_entrypoint(const char *dir, const char *content);
bool memdir_entrypoint_exists(const char *dir);
void memdir_free_headers(memdir_header_t *hdrs, int count);

/* ── Per-Agent Memory Scopes ── */

typedef enum { AGENT_MEM_USER = 0, AGENT_MEM_PROJECT, AGENT_MEM_LOCAL } agent_mem_scope_t;

int agent_memory_dir(char *out, size_t sz, const char *agent_type, agent_mem_scope_t scope);
int agent_memory_ensure(const char *agent_type, agent_mem_scope_t scope);
char *agent_memory_prompt(const char *agent_type, agent_mem_scope_t scope, int max_tokens);

/* ── Process Output Buffer ── */

typedef struct {
    char *data;
    size_t capacity;
    size_t head;
    size_t len;
} proc_output_buf_t;

void proc_outbuf_init(proc_output_buf_t *b, size_t cap);
void proc_outbuf_free(proc_output_buf_t *b);
void proc_outbuf_append(proc_output_buf_t *b, const char *data, size_t n);
char *proc_outbuf_snapshot(const proc_output_buf_t *b);
size_t proc_outbuf_len(const proc_output_buf_t *b);

/* ── Agent Color Assignment ── */

const char *agent_color_for(const char *agent_type);
void agent_color_reset(void);

/* ── Compact Warning — UX feedback for context pressure ── */

void compact_warn_suppress(void);
void compact_warn_clear(void);
bool compact_warn_is_suppressed(void);

/* ── Exec Policy — policy-driven execution control ── */

typedef enum { EP_FULL_ACCESS = 0, EP_READ_ONLY, EP_SANDBOXED, EP_CUSTOM } ep_mode_t;

void exec_policy_init(void);
void exec_policy_free(void);
void exec_policy_set_mode(ep_mode_t mode);
ep_mode_t exec_policy_mode(void);
const char *exec_policy_mode_name(ep_mode_t m);
int exec_policy_grant_path(const char *pattern, bool writable);
bool exec_policy_check_path(const char *path, bool write_op);
int exec_policy_path_grant_count(void);
int exec_policy_grant_network(const char *host_pattern, bool allowed);
bool exec_policy_check_network(const char *host);
char *exec_policy_serialize(void);
int exec_policy_deserialize(const char *json);
char *exec_policy_format(void);

/* ── Process State Machine — fine-grained subprocess lifecycle ── */

typedef enum {
    PS_CREATED = 0,
    PS_STARTING,
    PS_RUNNING,
    PS_YIELDED,
    PS_COMPLETING,
    PS_EXITED,
    PS_KILLED,
    PS_TIMED_OUT
} pstate_phase_t;

void process_state_init(void);
void process_state_free(void);
int process_state_register(pid_t pid, const char *label, long timeout_ms);
int process_state_transition(pid_t pid, pstate_phase_t to);
int process_state_set_running(pid_t pid);
int process_state_set_exited(pid_t pid, int exit_code);
int process_state_set_killed(pid_t pid, int signal_num);
int process_state_yield(pid_t pid);
int process_state_resume(pid_t pid);
pstate_phase_t process_state_get(pid_t pid);
bool process_state_alive(pid_t pid);
long process_state_elapsed_ms(pid_t pid);
int process_state_total(void);
int process_state_active_count(void);
const char *pstate_phase_name(pstate_phase_t s);
char *process_state_format(void);

/* ── Output Truncation — line-aware head+tail truncation ── */

char *output_trunc_lines(const char *text, int head_lines, int tail_lines);
char *output_trunc_smart(const char *text, size_t max_bytes, int head_lines, int tail_lines);
char *output_trunc_tool_result(const char *text, const char *tool_name, size_t max_bytes);
int output_trunc_count_lines(const char *text);

/* ── Seek Match — fuzzy sequence matching for patches ── */

int seek_match(const char **lines, int n_lines, const char **pattern, int n_pat, int start,
               bool prefer_eof);
int seek_match_scored(const char **lines, int n_lines, const char **pattern, int n_pat, int start,
                      int *quality_out);

/* ── File Search Tool ── */

int file_search_exec(const cJSON *input, char **output, void *ctx);

/* ── Send Message Tool ── */

int send_msg_exec(const cJSON *input, char **output, void *ctx);

/* ── Image View Tool ── */

int image_view_exec(const cJSON *input, char **output, void *ctx);

/* ── Agent Health — rich lifecycle status tracking ── */

void agent_health_init(void);
void agent_health_free(void);
int agent_health_register(const char *agent_id);
int agent_health_set_phase(const char *agent_id, int phase);
int agent_health_record_turn(const char *agent_id);
int agent_health_record_tool(const char *agent_id, const char *tool_name);
int agent_health_record_error(const char *agent_id);
int agent_health_set_progress(const char *agent_id, const char *note);
int agent_health_unregister(const char *agent_id);
long agent_health_idle_ms(const char *agent_id);
int agent_health_count(void);
char *agent_health_format(const char *agent_id);
char *agent_health_format_all(void);

/* ── Agent Control — pause / resume / cancel / restart ── */

int agent_control_pause(const char *agent_id);
int agent_control_resume(const char *agent_id);
int agent_control_cancel(const char *agent_id);
int agent_control_restart(const char *agent_id);
int agent_control_exec(const cJSON *input, char **output, void *ctx);

/* ── Diff Rendering — ANSI-colored diff output ── */

char *diff_render_colorize(const char *diff_text);
char *diff_render_stat(const char *diff_text);
int diff_render_count_changes(const char *diff_text, int *added, int *removed);
int diff_render_extract_files(const char *diff_text, const char **out, int max);
int diff_render_file_count(const char *diff_text);
char *diff_render_summary(const char *diff_text);
char *diff_render_word_highlight(const char *old_line, const char *new_line);

/* ── Markdown Rendering — terminal markdown display ── */

char *md_render_to_terminal(const char *markdown);
int md_render_line_count(const char *markdown);

/* ── Thread State Persistence — snapshot save/load ── */

void thread_state_init(void);
void thread_state_free(void);
int thread_state_capture(const char *thread_id, const char *session_id, int turn, int tool_calls,
                         int token_usage, const char *phase);
int thread_state_annotate(int idx, const char *note);
int thread_state_save(const char *dir);
int thread_state_load(const char *dir);
int thread_state_find(const char *thread_id);
int thread_state_count(void);
char *thread_state_format(int idx);
char *thread_state_format_all(void);

/* ── Compact Templates — structured compaction prompts ── */

void compact_tpl_init(void);
void compact_tpl_free(void);
int compact_tpl_add(const char *name, const char *body);
const char *compact_tpl_get(const char *name);
const char *compact_tpl_default(void);
const char *compact_tpl_summary_prefix(void);
const char *compact_tpl_mid_turn_prefix(void);
int compact_tpl_count(void);
char *compact_tpl_render(const char *name, int msg_count, int token_count, const char *session_id);
char *compact_tpl_format_list(void);

/* ── Vim Mode — terminal vim-style keybinding for input editing ── */

void vim_mode_init(void);
void vim_mode_free(void);
void vim_mode_set_enabled(bool on);
bool vim_mode_enabled(void);
/* vim_mode_state() and vim_state_name() use internal enum — see vim_mode.c */
int vim_motion(const char *buf, int len, int pos, char motion, int count);
int vim_operator(char *buf, int *len, int pos, char op, int start, int end);
int vim_text_object(const char *buf, int len, int pos, char obj, bool inner, int *out_start,
                    int *out_end);
const char *vim_register_content(char reg);
char *vim_mode_format(void);

/* ── Remote Bridge — remote session connectivity ── */

void remote_bridge_init(void);
void remote_bridge_free(void);
int remote_bridge_connect(const char *host, int port, const char *session_id);
int remote_bridge_send(int idx, const char *data, int len);
int remote_bridge_recv(int idx, char *buf, int buf_sz);
int remote_bridge_disconnect(int idx);
/* remote_bridge_state() uses internal enum — see remote_bridge.c */
int remote_bridge_count(void);
int remote_bridge_active_count(void);
/* bridge_state_name() uses internal enum — see remote_bridge.c */
char *remote_bridge_format(int idx);
char *remote_bridge_format_all(void);

/* ── Output Style — structured output formatting ── */

void output_style_init(void);
void output_style_free(void);
/* output_style_set/get use internal enum — see output_style.c */
void output_style_set_color(bool on);
bool output_style_color(void);
/* output_style_name/parse use internal enum — see output_style.c */
char *output_style_header(const char *title);
char *output_style_tool_call(const char *tool_name, const char *args_preview);
char *output_style_text(const char *text);
char *output_style_footer(int tokens_in, int tokens_out, double cost);
char *output_style_format(void);

/* ── Keybindings — configurable keyboard shortcuts ── */

void keybindings_init(void);
void keybindings_free(void);
/* keybinding_add action param uses internal enum — see keybindings.c */
int keybinding_remove(int key, int modifiers);
/* keybinding_lookup returns internal enum — see keybindings.c */
int keybinding_count(void);
/* kb_action_name uses internal enum — see keybindings.c */
char *keybindings_format(void);

/* ── Realtime Conversation — streaming real-time session management ── */

void realtime_conv_init(void);
void realtime_conv_free(void);
int realtime_conv_begin(const char *session_id, const char *model);
int realtime_conv_feed_chunk(const char *text, int tokens, bool is_final);
int realtime_conv_complete(int input_tokens);
int realtime_conv_error(const char *msg);
/* realtime_conv_phase() and rtc_phase_name() use internal enum — see realtime_conv.c */
char *realtime_conv_assembled(void);
int realtime_conv_chunk_count(void);
int realtime_conv_tokens_in(void);
int realtime_conv_tokens_out(void);
char *realtime_conv_format(void);

/* ── Coordinator Mode — multi-task planning and orchestration ── */

void coordinator_mode_init(void);
void coordinator_mode_free(void);
int coordinator_mode_plan(const char *goal);
int coordinator_mode_add_task(const char *desc, int priority, int depends_on);
char *coordinator_mode_merge(void);
void coordinator_mode_reset(void);
/* coordinator_mode_phase() and cm_phase_name() use internal enum — see coordinator_mode.c */
int coordinator_mode_task_count(void);
int coordinator_mode_completed(void);
int coordinator_mode_failed(void);
int coordinator_mode_pending(void);
char *coordinator_mode_format(void);

/* ── Model Presets — reusable model configurations ── */

void model_presets_init(void);
void model_presets_set_provider(provider_type_t provider);
void model_presets_free(void);
int model_preset_add(const char *name, const char *model, int max_tokens, int context_window,
                     effort_level_t effort, bool thinking, const char *hint);
int model_preset_find(const char *name);
int model_preset_activate(const char *name);
int model_preset_active_idx(void);
const char *model_preset_model(int idx);
int model_preset_max_tokens(int idx);
int model_preset_context_window(int idx);
effort_level_t model_preset_effort(int idx);
bool model_preset_thinking(int idx);
double model_preset_temperature(int idx);
double model_preset_top_p(int idx);
int model_preset_count(void);
int model_preset_load_from_config(const cJSON *cfg);
char *model_preset_format_all(void);

/* ── Doom Loop Detection ── */
typedef struct {
    uint32_t hash;
    char tool_name[DYN_TOOL_NAME_MAX];
} doom_loop_entry_snapshot_t;

typedef struct {
    doom_loop_entry_snapshot_t entries[DOOM_LOOP_WINDOW];
    int count;
    int fire_count;
    bool enabled;
} doom_loop_snapshot_t;

void doom_loop_init(void);
void doom_loop_reset(void);
void doom_loop_set_enabled(bool on);
bool doom_loop_record(const char *tool_name, const char *input_json);
int doom_loop_fire_count(void);
const char *doom_loop_last_tool(void);
char *doom_loop_format_status(void);
void doom_loop_snapshot(doom_loop_snapshot_t *out);
void doom_loop_restore(const doom_loop_snapshot_t *snap);

/* ── Batch Runner ── */
int batch_load_prompts(const char *path);
int batch_count(void);
int batch_completed(void);
int batch_failed(void);
const char *batch_prompt_at(int idx);
const char *batch_id_at(int idx);
void batch_record_result(int idx, const char *result, bool success);
void batch_save_checkpoint(int idx);
int batch_resume_index(void);
char *batch_format_summary(void);
void batch_free(void);

/* ── Workflow Scripts ── */
void workflow_discover(void);
int workflow_count(void);
const char *workflow_name(int idx);
const char *workflow_desc(int idx);
int workflow_find(const char *name);
int workflow_run(int idx, const char *args, char **output);
char *workflow_list_format(void);
void workflow_register_tool(void);

/* ── History Snip ── */
void snip_register_tool(void);

/* ── Process Monitor ── */
void pmon_init(void);
int pmon_track(pid_t pid, const char *label);
int pmon_kill(pid_t pid);
char *pmon_format_dashboard(void);
void proc_monitor_register_tool(void);

/* ── Incremental JSON Streamer — parse tool args as they stream in ── */

typedef void (*json_streamer_cb_t)(const char *key, const char *value, int value_len, bool complete,
                                   void *ctx);

/* Opaque — full definition lives in json_streamer.c */
struct json_streamer;
typedef struct json_streamer json_streamer_t;

json_streamer_t *json_streamer_create(json_streamer_cb_t cb, void *ctx);
void json_streamer_free(json_streamer_t *js);
void json_streamer_reset(json_streamer_t *js);
int json_streamer_feed(json_streamer_t *js, const char *data, int len);
bool json_streamer_complete(const json_streamer_t *js);
int json_streamer_field_count(const json_streamer_t *js);
cJSON *json_streamer_to_json(const json_streamer_t *js);
json_streamer_t *json_streamer_pool_acquire(json_streamer_cb_t cb, void *ctx);
void json_streamer_pool_release(json_streamer_t *js);

/* Lenient parse for tool `arguments` JSON (small models / gateways). */
cJSON *tool_args_parse_json(const char *args_str);

/* ── Hook Runtime Engine — lifecycle hook execution ── */

hook_decision_t hook_runtime_execute(hook_ctx_t *h, hook_event_t event, const char *tool_name,
                                     const char *tool_input, char **additional_ctx);
void hook_runtime_reset_stats(void);
int hook_runtime_total_runs(void);
char *hook_runtime_format_stats(void);

/* ── Session Snapshot — save/restore full session state ── */

int session_snapshot_save(const char *session_id, const void *conv, const char *model,
                          int input_tokens, int output_tokens, int ctx_pct, const char *goal);
int session_snapshot_load(const char *session_id, void *conv);
int session_snapshot_count(void);
void session_snapshot_free_all(void);

/* ── Rollout Tracker — session replay/reconstruction ── */

void rollout_tracker_init(const char *session_id);
void rollout_tracker_free(void);
int rollout_record_user_message(const char *text);
int rollout_record_assistant_message(const char *text);
int rollout_record_tool_call(const char *tool_name, const char *tool_id, const cJSON *input);
int rollout_record_tool_result(const char *tool_id, const char *output, bool is_error);
int rollout_record_compaction(int tokens_before, int tokens_after);
int rollout_record_handoff(const char *reason);
int rollout_record_checkpoint(const char *label);
int rollout_record_fork(const char *parent_session, int from_turn);
int rollout_record_settings(const char *key, const char *value);
int rollout_event_count(void);
int rollout_current_turn(void);
bool rollout_is_dirty(void);
int rollout_save(void);

/* ── Rollout Metadata — per-session aggregate statistics ── */

typedef enum {
    SESSION_SRC_CLI = 0,
    SESSION_SRC_SERVER,
    SESSION_SRC_SDK,
    SESSION_SRC_REMOTE,
    SESSION_SRC_AGENT
} session_source_t;

typedef struct {
    char session_id[PROTO_ID_LEN];
    char model[MODEL_NAME_MAX];
    char workspace[NC_PATH_MAX];
    char parent_session[PROTO_ID_LEN];
    session_source_t source;
    int turn_count;
    int tool_call_count;
    int compact_count;
    int input_tokens;
    int output_tokens;
    double cost;
    long started_at;
    long ended_at;
    char summary[CONV_SUMMARY_BODY_MAX];
} rollout_meta_t;

void rollout_meta_init(rollout_meta_t *m);
void rollout_meta_set_session(rollout_meta_t *m, const char *id, const char *model,
                              const char *workspace);
void rollout_meta_set_source(rollout_meta_t *m, session_source_t src);
void rollout_meta_set_parent(rollout_meta_t *m, const char *parent_id);
void rollout_meta_record_turn(rollout_meta_t *m, int tool_calls, int in_tok, int out_tok,
                              double turn_cost);
void rollout_meta_record_compact(rollout_meta_t *m);
void rollout_meta_finalize(rollout_meta_t *m, const char *summary);
char *rollout_meta_serialize(const rollout_meta_t *m);
int rollout_meta_deserialize(rollout_meta_t *m, const char *json);
long rollout_meta_duration_s(const rollout_meta_t *m);
char *rollout_meta_format(const rollout_meta_t *m);
int rollout_meta_save(const rollout_meta_t *m, const char *session_id);
const char *session_source_name(session_source_t src);
session_source_t session_source_parse(const char *name);

/* Mid-turn compaction modes (controls initial context injection) */
typedef enum { COMPACT_INJECT_NONE = 0, COMPACT_INJECT_BEFORE_LAST_USER } compact_inject_mode_t;

/* ── Process Hardening — security initialization ── */

int proc_harden_init(void);
bool proc_harden_core_dumps_off(void);
bool proc_harden_ptrace_denied(void);
int proc_harden_env_cleared(void);
char *proc_harden_format(void);

/* ── Collaboration V2 — enhanced multi-agent protocol ── */

void collab_v2_init(void);
void collab_v2_free(void);
int collab_v2_spawn(const char *agent_type, const char *message, int fork_mode, int parent_depth,
                    char *out_task_id, size_t id_sz);
int collab_v2_followup(const char *task_id, const char *message, char *out_new_id, size_t id_sz);
int collab_v2_complete(const char *task_id, const char *summary);
int collab_v2_active_count(void);
char *collab_v2_list_json(void);

/* ── Streaming Tool Executor — concurrency control ── */

void ste_init(void);
void ste_free(void);
void ste_discard(void);
int ste_add_tool(const char *call_id, const char *tool_name);
bool ste_can_start(int idx);
void ste_mark_started(int idx);
void ste_mark_completed(int idx, const char *result, int code);
int ste_pending_count(void);
int ste_completed_count(void);
int ste_total_count(void);
bool ste_has_error(void);
char *ste_format(void);

/* ── User Shell Command ── */

int usc_total_commands(void);
char *usc_format_stats(void);

/* ── Round Grouping — API-round-level compact boundaries ── */

int count_api_rounds(const void *msgs, int len);

/* ── Models Manager — remote model discovery ── */

void models_mgr_init(void);
void models_mgr_free(void);
int models_mgr_count(void);
bool models_mgr_stale(void);
int models_mgr_find(const char *id);
int models_mgr_context_window(int idx);
char *models_mgr_list_json(void);

/* ── Process Manager lifecycle ── */

void pm_init(void);
void pm_free(void);

/* ── Protocol event lifecycle ── */

void proto_init(void);
void proto_free(void);

/* ── Feature Flags lifecycle ── */

void ff_init(void);
void ff_free(void);

/* ── State DB lifecycle ── */

void state_db_init(void);
void state_db_free(void);

/* ── Input Guard — path sanitization and traversal prevention ── */

void input_guard_init(const char *project_root);
void input_guard_free(void);
void input_guard_set_allow_absolute(bool on);
bool input_guard_check_path(const char *path);
char *input_guard_resolve(const char *path, char *out, size_t out_sz);
bool input_guard_check_length(const char *input, size_t max);
const char *input_guard_diagnose(const char *path);
int input_guard_blocked_count(void);
int input_guard_checked_count(void);
char *input_guard_format(void);

/* ── Auto-Continue — handles truncated model responses ── */

void auto_continue_init(int max_retries);
void auto_continue_free(void);
bool auto_continue_enabled(void);
void auto_continue_set_enabled(bool on);
bool auto_continue_needs_escalation(void);
int auto_continue_escalated_tokens(void);
void auto_continue_mark_escalated(void);
bool auto_continue_should_retry(const char *finish_reason, const char *partial_text,
                                bool has_pending_tool_call);
const char *auto_continue_prompt(bool mid_tool_call);
void auto_continue_stitch(const char *continuation);
char *auto_continue_take_result(void);
void auto_continue_reset(void);
int auto_continue_retry_count(void);
int auto_continue_total_continuations(void);
bool auto_continue_is_continuing(void);
char *auto_continue_format(void);

/* ── Debug Allocation Tracker ── */

void alloc_track_init(void);
void alloc_track_free(void);
void alloc_track_record(void *ptr, size_t size, const char *file, int line);
void alloc_track_release(void *ptr);
void alloc_track_update(void *old_ptr, void *new_ptr, size_t new_size, const char *file, int line);
int alloc_track_leak_count(void);
size_t alloc_track_active_bytes(void);
size_t alloc_track_high_water(void);
char *alloc_track_report(int max_entries);

/* ── Color Palette — adaptive terminal color sequences ── */

void palette_init(void);
void palette_free(void);
int palette_capability(void);
const char *palette_seq(int role);
const char *palette_reset(void);
void palette_sprint(char *dst, size_t sz, int role, const char *text);
char *palette_wrap(int role, const char *text);
void palette_force_cap(int cap);

/* ── Status Bar — compact session state display ── */

void sbar_init(void);
void sbar_free(void);
void sbar_set_model(const char *m);
void sbar_set_mode(const char *m);
void sbar_set_tokens(int in, int out);
void sbar_set_cost(double c);
void sbar_set_turns(int t);
void sbar_set_elapsed(int s);
void sbar_set_active(bool a);
char *sbar_render_for_width(int width);
char *sbar_render(void);
bool sbar_is_dirty(void);
void sbar_print(void);
const char *sbar_get_model(void);
const char *sbar_get_mode(void);
int sbar_get_turns(void);

/* ── Conversation Rewind — turn-level backtracking ── */

void conv_rewind_init(void);
void conv_rewind_free(void);
bool conv_rewind_checkpoint(int turn_idx, int msg_count, const char *label,
                            const char *snapshot_json);
int conv_rewind_count(void);
int conv_rewind_cursor(void);
bool conv_rewind_step_back(void);
bool conv_rewind_step_forward(void);
bool conv_rewind_goto(int idx);
const char *conv_rewind_snapshot(int idx);
int conv_rewind_turn_at(int idx);
int conv_rewind_msg_count_at(int idx);
const char *conv_rewind_label_at(int idx);
bool conv_rewind_truncate_after(int idx);
char *conv_rewind_apply(int idx);
char *conv_rewind_list(void);

/* ── Clipboard — system clipboard access ── */

char *clipboard_read_text(void);
char *clipboard_read_image(void);
bool clipboard_write_text(const char *text, size_t len);
bool clipboard_available(void);
bool clipboard_has_image(void);

/* ── Key Hints — contextual shortcut display ── */

char *key_hint_render(int ctx, int max_width);
char *key_hint_render_plain(int ctx);
int key_hint_count(int ctx);

/* ── Line Wrapping — ANSI-aware word wrap ── */

char *line_wrap(const char *text, int width);
int line_wrap_count(const char *text, int width);
int line_visible_width(const char *line);
void line_copy_prefix_cells(const char *src, int max_cells, char *out, size_t out_sz);

/* ── Side Query — lightweight /btw forked Q&A ── */

bool side_query_is_trigger(const char *input);
const char *side_query_extract(const char *input);
int side_query_run(const char *question, const conversation_t *conv, const void *cfg);

/* ── Message Collapse — group consecutive read/search ops ── */

typedef enum {
    MSG_COLLAPSE_PASSTHROUGH,
    MSG_COLLAPSE_READ_SEARCH,
    MSG_COLLAPSE_BACKGROUND,
    MSG_COLLAPSE_HOOK
} msg_collapse_kind_t;

typedef struct {
    msg_collapse_kind_t kind;
    int start;
    int count;
    int reads;
    int searches;
    int lists;
    char path_hint[MSG_COLLAPSE_PATH_MAX];
} msg_collapse_entry_t;

int msg_collapse_scan(const message_t *msgs, int len, msg_collapse_entry_t *out, int out_cap);
char *msg_collapse_format(const msg_collapse_entry_t *entry);
int msg_collapse_hook_summaries(const message_t *msgs, int len, int *merged_indices, int cap);

/* ── Shell Completion — zsh/bash/fish script generation ── */

char *shell_completion_generate(const char *shell_name);
const char *shell_completion_detect(void);
int shell_completion_install(const char *shell_name);

/* ── Git Operation Tracking — classify git ops from bash commands ── */

typedef enum {
    GIT_OP_NONE = 0,
    GIT_OP_COMMIT,
    GIT_OP_PUSH,
    GIT_OP_PULL,
    GIT_OP_BRANCH_CREATE,
    GIT_OP_BRANCH_SWITCH,
    GIT_OP_BRANCH_DELETE,
    GIT_OP_MERGE,
    GIT_OP_REBASE,
    GIT_OP_STASH,
    GIT_OP_TAG,
    GIT_OP_RESET,
    GIT_OP_CHERRY_PICK,
    GIT_OP_PR_CREATE,
    GIT_OP_PR_MERGE,
    GIT_OP_STATUS,
    GIT_OP_DIFF,
    GIT_OP_LOG,
    GIT_OP_ADD,
    GIT_OP_CLONE,
    GIT_OP_FETCH,
    GIT_OP_INIT,
    GIT_OP_SUBMODULE,
    GIT_OP_WORKTREE,
} git_op_kind_t;

typedef struct {
    git_op_kind_t kind;
    bool destructive;
    const char *branch_name;
} git_op_t;

git_op_t git_ops_classify(const char *cmd);
const char *git_op_name(git_op_kind_t kind);
bool git_op_is_mutating(git_op_kind_t kind);
void git_ops_record(git_op_kind_t kind, bool destructive);
int git_ops_session_count(git_op_kind_t kind);
int git_ops_total_destructive(void);
char *git_ops_format_summary(void);
void git_ops_reset(void);

/* ── Session Scratch Directory — per-session temp workspace ── */

int session_scratch_init(const char *session_id);
const char *session_scratch_dir(void);
bool session_scratch_active(void);
char *session_scratch_file(const char *name);
void session_scratch_cleanup(void);
char *session_scratch_prompt(void);

/* ── Context Token Stats — per-category token distribution ── */

typedef struct {
    char name[NC_FMT_MAX];
    int request_count;
    int result_tokens;
} ctx_tool_type_stat_t;

typedef struct {
    char path[NC_PATH_MAX];
    int count;
    int total_tokens;
} ctx_dup_read_t;

typedef struct {
    int total_tokens;
    int system_tokens;
    int user_tokens;
    int user_count;
    int assistant_tokens;
    int assistant_count;
    int tool_request_tokens;
    int tool_request_count;
    int tool_result_tokens;
    int tool_result_count;
    ctx_tool_type_stat_t tool_types[CTX_STATS_TOOL_MAX];
    int tool_type_count;
    ctx_dup_read_t dup_reads[CTX_STATS_DUP_READ_MAX];
    int dup_read_count;
} ctx_token_stats_t;

void ctx_token_stats_init(ctx_token_stats_t *s);
void ctx_token_stats_analyze(ctx_token_stats_t *s, const message_t *msgs, int len);
char *ctx_token_stats_format(const ctx_token_stats_t *s);

/* ── Idle Guard — daemon/server mode auto-exit on inactivity ── */

/* ── Token Budget Shorthand — parse "+500k" / "spend 2M tokens" ── */

long budget_parse_shorthand(const char *text);

/* ── Cache Strategy — prompt cache breakpoint placement ── */

int cache_strategy_mark(message_t *msgs, int len, int max_breakpoints);
int cache_strategy_system_and_n(int msg_count, int max_bp);

/* ── LLM Session Title — async title generation ── */

void title_llm_generate_async(const char *user_msg, const char *asst_msg,
                              void (*on_done)(const char *, void *), void *ctx);
const char *title_llm_get(void);
bool title_llm_is_ready(void);
void title_llm_set(const char *title);
void title_llm_reset(void);

/* ── Idle Guard — daemon/server mode auto-exit on inactivity ── */

/* ── Argument Substitution — $ARGUMENTS placeholder expansion ── */

char *arg_subst_expand(const char *tmpl, const char *args_str, const char **named_keys,
                       int num_names);

/* ── Generated File Detection ── */

bool generated_detect(const char *path);
bool generated_is_lock_file(const char *path);
bool generated_is_vendor(const char *path);

/* ── Activity Heatmap — terminal activity visualization ── */

char *activity_heatmap_render(const int *daily_counts, int num_days, int term_width);

/* ── File Read Cache — mtime-based content cache ── */

const char *frc_get(const char *path, size_t *out_len);
int frc_put(const char *path, const char *content, size_t len);
void frc_invalidate(const char *path);
void frc_clear(void);
int frc_size(void);

/* ── Skill Usage Tracking — frequency + recency scoring ── */

void skill_usage_record(const char *name);
double skill_usage_score(const char *name);
int skill_usage_count(const char *name);
int skill_usage_total(void);
void skill_usage_reset(void);

/* ── Command Fuzzy Matching — slash command suggestions ── */

typedef struct {
    char name[CMD_FUZZY_MAX_LEN];
    int score;
} cmd_fuzzy_result_t;

int cmd_fuzzy_match(const char *query, const char **commands, int ncmds, cmd_fuzzy_result_t *out,
                    int max_results);

/* ── Idle Guard — daemon/server mode auto-exit on inactivity ── */

void idle_guard_init(int timeout_ms);
void idle_guard_touch(void);
void idle_guard_start(void);
void idle_guard_stop(void);
bool idle_guard_is_expired(void);
int idle_guard_remaining_ms(void);
bool idle_guard_enabled(void);
int idle_guard_timeout_ms(void);

/* ── API Preconnect — TCP+TLS handshake warmup ── */

void api_preconnect(const char *base_url);
void api_preconnect_set_enabled(bool on);
bool api_preconnect_fired(void);
long api_preconnect_elapsed_ms(void);
int api_preconnect_result(void);
void api_preconnect_reset(void);

/* ── Startup Profiler — init phase timing ── */

void sprof_init(void);
void sprof_checkpoint(const char *name);
int sprof_count(void);
bool sprof_enabled(void);
void sprof_set_enabled(bool on);
char *sprof_report(void);
long sprof_total_ms(void);

/* ── Session Registry — concurrent session PID files ── */

int sreg_register(const char *session_id);
void sreg_unregister(void);
bool sreg_is_registered(void);
int sreg_list_live(char **out_buf);
int sreg_count_live(void);

/* ── Model Deprecation — deprecated model warnings ── */

bool depr_is_deprecated(const char *model_id);
const char *depr_display_name(const char *model_id);
const char *depr_retirement_date(const char *model_id);
char *depr_warning_message(const char *model_id);
int depr_table_size(void);

/* ── Prompt Section Registry — modular memoized system prompt sections ── */

typedef char *(*psec_compute_fn)(void *user_data);

void psec_init(void);
int psec_register(const char *name, psec_compute_fn fn, void *user_data, bool is_volatile);
const char *psec_resolve(const char *name);
int psec_resolve_all(char **out_buf, int max_parts);
void psec_invalidate(const char *name);
void psec_invalidate_all(void);
int psec_count(void);
bool psec_is_volatile(const char *name);

/* ── Verification Trigger — auto-verify after non-trivial changes ── */

void vtrig_init(void);
void vtrig_set_threshold(int n);
int vtrig_threshold(void);
void vtrig_record_edit(const char *path);
bool vtrig_should_verify(void);
int vtrig_distinct_files(void);
int vtrig_total_edits(void);
void vtrig_acknowledge(void);
void vtrig_reset(void);
char *vtrig_summary(void);

/* ── Output Length Anchors — numeric word limits in system prompt ── */

void oa_init(void);
void oa_set_enabled(bool on);
bool oa_enabled(void);
void oa_set_limits(int inter_tool, int final_response);
char *oa_build_section(void);
int oa_inter_tool_limit(void);
int oa_final_response_limit(void);

/* ── Knowledge Cutoff — model training data cutoff dates ── */

const char *kc_lookup(const char *model_id);
char *kc_build_prompt(const char *model_id);
int kc_table_size(void);

#endif /* NEVERCODE_H */
