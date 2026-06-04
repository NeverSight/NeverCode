#include "nevercode.h"
#include "defaults.h"

#define TH_TAG_OPEN THINKING_TAG_OPEN
#define TH_TAG_CLOSE THINKING_TAG_CLOSE
#define TH_OPEN_LEN THINKING_TAG_OPEN_LEN
#define TH_CLOSE_LEN THINKING_TAG_CLOSE_LEN

typedef enum { TH_TEXT, TH_INSIDE } th_state_t;

typedef struct {
    char *content;
    int len;
    int cap;
} th_block_t;

static struct {
    th_state_t state;
    th_block_t blocks[THINKING_MAX_BLOCKS];
    int block_count;
    int total_tokens;
    int budget;
    bool enabled;
} g_think;

static void blk_append(th_block_t *b, const char *d, int n) {
    if (b->len + n + 1 > b->cap) {
        int nc = b->cap ? b->cap * 2 : THINKING_BLOCK_INIT_CAP;
        while (nc < b->len + n + 1)
            nc *= 2;
        char *tmp = realloc(b->content, nc);
        if (!tmp)
            return;
        b->content = tmp;
        b->cap = nc;
    }
    memcpy(b->content + b->len, d, n);
    b->len += n;
    b->content[b->len] = '\0';
}

void think_init(int budget) {
    memset(&g_think, 0, sizeof(g_think));
    g_think.budget = budget > 0 ? budget : DEFAULT_MAX_TOKENS;
    g_think.enabled = true;
}

void think_cleanup(void) {
    for (int i = 0; i < g_think.block_count; i++)
        free(g_think.blocks[i].content);
    memset(&g_think, 0, sizeof(g_think));
}

int think_feed(const char *data, int len, char *vis, int vcap) {
    int vw = 0, i = 0;
    while (i < len) {
        if (g_think.state == TH_TEXT) {
            if (i + TH_OPEN_LEN <= len && strncmp(data + i, TH_TAG_OPEN, TH_OPEN_LEN) == 0) {
                g_think.state = TH_INSIDE;
                if (g_think.block_count < THINKING_MAX_BLOCKS)
                    g_think.block_count++;
                i += TH_OPEN_LEN;
                continue;
            }
            if (vw < vcap)
                vis[vw++] = data[i];
            i++;
        } else {
            if (i + TH_CLOSE_LEN <= len && strncmp(data + i, TH_TAG_CLOSE, TH_CLOSE_LEN) == 0) {
                g_think.state = TH_TEXT;
                i += TH_CLOSE_LEN;
                continue;
            }
            int bi = g_think.block_count - 1;
            if (bi >= 0 && bi < THINKING_MAX_BLOCKS)
                blk_append(&g_think.blocks[bi], data + i, 1);
            g_think.total_tokens++;
            i++;
        }
    }
    return vw;
}

int think_block_count(void) {
    return g_think.block_count;
}
int think_total_tokens(void) {
    return g_think.total_tokens;
}
bool think_over_budget(void) {
    return g_think.total_tokens > g_think.budget;
}
bool think_is_active(void) {
    return g_think.state == TH_INSIDE;
}
void think_set_enabled(bool on) {
    g_think.enabled = on;
}
bool think_is_enabled(void) {
    return g_think.enabled;
}

const char *think_block_content(int i) {
    return (i >= 0 && i < g_think.block_count) ? g_think.blocks[i].content : NULL;
}

int think_block_len(int i) {
    return (i >= 0 && i < g_think.block_count) ? g_think.blocks[i].len : 0;
}

int think_separate(const char *full, int fl, char *vis, int vc, char *th, int tc) {
    int vl = 0, tl = 0;
    bool inside = false;
    for (int i = 0; i < fl;) {
        if (!inside) {
            if (i + TH_OPEN_LEN <= fl && strncmp(full + i, TH_TAG_OPEN, TH_OPEN_LEN) == 0) {
                inside = true;
                i += TH_OPEN_LEN;
                continue;
            }
            if (vl < vc)
                vis[vl++] = full[i];
            i++;
        } else {
            if (i + TH_CLOSE_LEN <= fl && strncmp(full + i, TH_TAG_CLOSE, TH_CLOSE_LEN) == 0) {
                inside = false;
                i += TH_CLOSE_LEN;
                continue;
            }
            if (tl < tc)
                th[tl++] = full[i];
            i++;
        }
    }
    if (vl < vc)
        vis[vl] = '\0';
    if (tl < tc)
        th[tl] = '\0';
    return vl;
}
