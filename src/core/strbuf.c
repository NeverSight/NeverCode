#include "nevercode.h"
#include <stdarg.h>

int nc_mkdir_p(const char *path) {
    if (!path || !*path)
        return -1;
    char tmp[NC_PATH_MAX];
    nc_strlcpy(tmp, path, sizeof(tmp));
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, NC_DIR_MODE);
            *p = '/';
        }
    }
    return (mkdir(tmp, NC_DIR_MODE) == 0 || errno == EEXIST) ? 0 : -1;
}

int nc_write_file_atomic(const char *path, const void *data, size_t len) {
    if (!path || !path[0] || (len > 0 && !data))
        return -1;

    char tmp[NC_PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp))
        return -1;

    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;

    int rc = 0;
    const char *buf = data ? (const char *)data : "";
    if (len > 0 && fwrite(buf, 1, len, f) != len)
        rc = -1;
    if (rc == 0 && fflush(f) != 0)
        rc = -1;
    if (rc == 0 && fsync(fileno(f)) != 0)
        rc = -1;
    if (fclose(f) != 0)
        rc = -1;
    if (rc != 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

void strbuf_init(strbuf_t *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void strbuf_append(strbuf_t *sb, const char *s, size_t n) {
    if (n == 0)
        return;
    size_t need = sb->len + n + 1;
    if (need > sb->cap) {
        size_t newcap = sb->cap ? sb->cap * 2 : STRBUF_INIT_CAP;
        while (newcap < need)
            newcap *= 2;
        char *tmp = realloc(sb->data, newcap);
        if (!tmp)
            return;
        sb->data = tmp;
        sb->cap = newcap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void strbuf_append_str(strbuf_t *sb, const char *s) {
    if (s)
        strbuf_append(sb, s, strlen(s));
}

void strbuf_appendf(strbuf_t *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed <= 0) {
        va_end(ap2);
        return;
    }

    size_t need = sb->len + (size_t)needed + 1;
    if (need > sb->cap) {
        size_t newcap = sb->cap ? sb->cap * 2 : STRBUF_INIT_CAP;
        while (newcap < need)
            newcap *= 2;
        char *tmp = realloc(sb->data, newcap);
        if (!tmp) {
            va_end(ap2);
            return;
        }
        sb->data = tmp;
        sb->cap = newcap;
    }
    vsnprintf(sb->data + sb->len, (size_t)needed + 1, fmt, ap2);
    sb->len += (size_t)needed;
    va_end(ap2);
}

void strbuf_clear(strbuf_t *sb) {
    sb->len = 0;
    if (sb->data)
        sb->data[0] = '\0';
}

void strbuf_free(strbuf_t *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

char *strbuf_detach(strbuf_t *sb) {
    char *p = sb->data;
    sb->data = NULL;
    sb->len = sb->cap = 0;
    return p;
}

char *strbuf_read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    if (sz == 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len)
        *out_len = n;
    return buf;
}

char *strbuf_read_file_max(const char *path, size_t *out_len, size_t max_bytes) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    if (sz == 0 || (size_t)sz > max_bytes) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len)
        *out_len = n;
    return buf;
}

char *strbuf_read_file_text(const char *path, size_t *out_len) {
    char *buf = strbuf_read_file(path, out_len);
    if (buf)
        return buf;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0) {
        if (out_len)
            *out_len = 0;
        return nc_xstrdup("");
    }
    return NULL;
}

int nc_exec_quiet(const char *cmd) {
    if (!cmd)
        return -1;
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int nul = nc_open_devnull(O_RDWR);
        if (nul >= 0) {
            dup2(nul, STDIN_FILENO);
            dup2(nul, STDOUT_FILENO);
            dup2(nul, STDERR_FILENO);
            close(nul);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

const char *nc_get_proxy_env(void) {
    static const char *const vars[] = {ENV_HTTPS_PROXY,   ENV_HTTPS_PROXY_LC, ENV_HTTP_PROXY,
                                       ENV_HTTP_PROXY_LC, ENV_ALL_PROXY,      ENV_ALL_PROXY_LC};
    for (size_t i = 0; i < (size_t)NC_ARRAY_LEN(vars); i++) {
        const char *v = getenv(vars[i]);
        if (v && v[0])
            return v;
    }
    return NULL;
}

static const char *g_home_cached;
static bool g_home_probed;

const char *nc_home_dir(void) {
    if (!g_home_probed) {
        const char *h = getenv(ENV_HOME);
        g_home_cached = (h && h[0]) ? h : NULL;
        g_home_probed = true;
    }
    return g_home_cached;
}

void nc_home_dir_reset(void) {
    g_home_probed = false;
    g_home_cached = NULL;
}

static const char *g_user_cached;
static bool g_user_probed;

const char *nc_user(void) {
    if (!g_user_probed) {
        const char *u = getenv(ENV_USER);
        g_user_cached = (u && u[0]) ? u : NULL;
        g_user_probed = true;
    }
    return g_user_cached;
}

static const char *g_shell_cached;
static bool g_shell_probed;

const char *nc_shell_env(void) {
    if (!g_shell_probed) {
        const char *s = getenv(ENV_SHELL);
        g_shell_cached = (s && s[0]) ? s : NULL;
        g_shell_probed = true;
    }
    return g_shell_cached;
}

static const char *g_tmpdir_cached;
static bool g_tmpdir_probed;

const char *nc_tmpdir(void) {
    if (!g_tmpdir_probed) {
        const char *t = getenv(ENV_TMPDIR);
        g_tmpdir_cached = (t && t[0]) ? t : "/tmp";
        g_tmpdir_probed = true;
    }
    return g_tmpdir_cached;
}

static const char *g_term_cached;
static bool g_term_probed;

const char *nc_term_env(void) {
    if (!g_term_probed) {
        const char *t = getenv(ENV_TERM);
        g_term_cached = (t && t[0]) ? t : NULL;
        g_term_probed = true;
    }
    return g_term_cached;
}

void nc_env_reset(void) {
    nc_home_dir_reset();
    g_user_probed = false;
    g_user_cached = NULL;
    g_shell_probed = false;
    g_shell_cached = NULL;
    g_tmpdir_probed = false;
    g_tmpdir_cached = NULL;
    g_term_probed = false;
    g_term_cached = NULL;
}

bool nc_cmd_exists(const char *name) {
    if (!name || !*name || strchr(name, '/'))
        return false;
    const char *path_env = getenv(ENV_PATH);
    if (!path_env || !*path_env)
        return false;
    char probe[NC_PATH_MAX];
    const char *p = path_env;
    while (*p) {
        const char *sep = strchr(p, ':');
        size_t dlen = sep ? (size_t)(sep - p) : strlen(p);
        size_t nlen = strlen(name);
        if (dlen > 0 && dlen + 1 + nlen < sizeof(probe)) {
            memcpy(probe, p, dlen);
            probe[dlen] = '/';
            memcpy(probe + dlen + 1, name, nlen + 1);
            if (access(probe, X_OK) == 0)
                return true;
        }
        if (!sep)
            break;
        p = sep + 1;
    }
    return false;
}
