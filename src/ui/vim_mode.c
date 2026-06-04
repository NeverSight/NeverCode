#include "nevercode.h"
#include "defaults.h"

typedef enum { VIM_NORMAL = 0, VIM_INSERT, VIM_VISUAL, VIM_COMMAND } vim_state_t;

typedef struct {
    vim_state_t state;
    int cursor, mark, count;
    char pending_op;
    char last_search[VIM_SEARCH_MAX];
    char registers[VIM_REGISTER_COUNT][VIM_REGISTER_SIZE];
    bool enabled;
    int total_commands, total_motions;
} vim_ctx_t;

static vim_ctx_t g_vim;

void vim_mode_init(void) {
    memset(&g_vim, 0, sizeof(g_vim));
    g_vim.state = VIM_INSERT;
}

void vim_mode_free(void) {
    memset(&g_vim, 0, sizeof(g_vim));
}

void vim_mode_set_enabled(bool on) {
    g_vim.enabled = on;
}

bool vim_mode_enabled(void) {
    return g_vim.enabled;
}

static int wf(const char *b, int l, int p) {
    if (p >= l)
        return l;
    bool iw = isalnum((unsigned char)b[p]) || b[p] == '_';
    while (p < l) {
        bool c = isalnum((unsigned char)b[p]) || b[p] == '_';
        if (iw && !c)
            iw = false;
        else if (!iw && c)
            break;
        p++;
    }
    return p;
}

static int wb(const char *b, int l, int p) {
    (void)l;
    if (p <= 0)
        return 0;
    p--;
    while (p > 0 && !isalnum((unsigned char)b[p]) && b[p] != '_')
        p--;
    while (p > 0 && (isalnum((unsigned char)b[p - 1]) || b[p - 1] == '_'))
        p--;
    return p;
}

int vim_motion(const char *buf, int len, int pos, char m, int cnt) {
    if (!g_vim.enabled || !buf)
        return pos;
    g_vim.total_motions++;
    int n = cnt > 0 ? cnt : 1;

    switch (m) {
    case 'h':
        return pos - n >= 0 ? pos - n : 0;
    case 'l':
        return pos + n < len ? pos + n : len > 0 ? len - 1 : 0;
    case '0':
        return 0;
    case '$':
        return len > 0 ? len - 1 : 0;
    case '^': {
        int i = 0;
        while (i < len && isspace((unsigned char)buf[i]))
            i++;
        return i < len ? i : 0;
    }
    case 'w': {
        int p = pos;
        for (int i = 0; i < n; i++)
            p = wf(buf, len, p);
        return p < len ? p : (len > 0 ? len - 1 : 0);
    }
    case 'b': {
        int p = pos;
        for (int i = 0; i < n; i++)
            p = wb(buf, len, p);
        return p;
    }
    case 'e': {
        int p = pos;
        for (int i = 0; i < n; i++) {
            p++;
            while (p < len && !isalnum((unsigned char)buf[p]) && buf[p] != '_')
                p++;
            while (p < len - 1 && (isalnum((unsigned char)buf[p + 1]) || buf[p + 1] == '_'))
                p++;
        }
        return p < len ? p : (len > 0 ? len - 1 : 0);
    }
    default:
        return pos;
    }
}

static void yr(const char *buf, int s, int e, char r) {
    if (s > e) {
        int t = s;
        s = e;
        e = t;
    }
    int idx = (r >= 'a' && r <= 'z') ? (r - 'a') : 0;
    int l = e - s;
    if (l >= VIM_REGISTER_SIZE)
        l = VIM_REGISTER_SIZE - 1;
    memcpy(g_vim.registers[idx], buf + s, (size_t)l);
    g_vim.registers[idx][l] = '\0';
}

int vim_operator(char *buf, int *len, int pos, char op, int start, int end) {
    if (!g_vim.enabled || !buf || !len)
        return pos;
    g_vim.total_commands++;
    if (start > end) {
        int t = start;
        start = end;
        end = t;
    }

    switch (op) {
    case 'd':
        yr(buf, start, end, '"');
        {
            int sp = end - start;
            memmove(buf + start, buf + end, (size_t)(*len - end));
            *len -= sp;
            buf[*len] = '\0';
        }
        return start < *len ? start : (*len > 0 ? *len - 1 : 0);
    case 'y':
        yr(buf, start, end, '"');
        return pos;
    case 'c':
        yr(buf, start, end, '"');
        {
            int sp = end - start;
            memmove(buf + start, buf + end, (size_t)(*len - end));
            *len -= sp;
            buf[*len] = '\0';
            g_vim.state = VIM_INSERT;
        }
        return start;
    default:
        return pos;
    }
}

int vim_text_object(const char *buf, int len, int pos, char obj, bool inner, int *os, int *oe) {
    if (!buf || pos < 0 || pos >= len)
        return -1;

    if (obj == 'w') {
        int s = pos, e = pos;
        while (s > 0 && (isalnum((unsigned char)buf[s - 1]) || buf[s - 1] == '_'))
            s--;
        while (e < len && (isalnum((unsigned char)buf[e]) || buf[e] == '_'))
            e++;
        if (!inner)
            while (e < len && isspace((unsigned char)buf[e]))
                e++;
        *os = s;
        *oe = e;
        return 0;
    }

    char open = 0, close = 0;
    switch (obj) {
    case '(':
    case ')':
        open = '(';
        close = ')';
        break;
    case '{':
    case '}':
        open = '{';
        close = '}';
        break;
    case '[':
    case ']':
        open = '[';
        close = ']';
        break;
    default:
        return -1;
    }

    int depth = 0, s = pos, e = pos;
    for (int i = pos; i >= 0; i--) {
        if (buf[i] == close)
            depth++;
        if (buf[i] == open) {
            depth--;
            if (depth < 0) {
                s = i;
                break;
            }
        }
    }
    depth = 0;
    for (int i = pos; i < len; i++) {
        if (buf[i] == open)
            depth++;
        if (buf[i] == close) {
            depth--;
            if (depth < 0) {
                e = i + 1;
                break;
            }
        }
    }
    if (inner) {
        s++;
        if (e > s)
            e--;
    }
    *os = s;
    *oe = e;
    return 0;
}

const char *vim_register_content(char reg) {
    int idx = (reg >= 'a' && reg <= 'z') ? (reg - 'a') : 0;
    return g_vim.registers[idx];
}

char *vim_mode_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Vim: %s, enabled=%s, commands=%d, motions=%d",
                   g_vim.state == VIM_NORMAL   ? "NORMAL"
                   : g_vim.state == VIM_INSERT ? "INSERT"
                   : g_vim.state == VIM_VISUAL ? "VISUAL"
                                               : "COMMAND",
                   g_vim.enabled ? "yes" : "no", g_vim.total_commands, g_vim.total_motions);
    return strbuf_detach(&sb);
}
