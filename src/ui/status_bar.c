/*
 * status_bar.c — compact status line display
 *
 * Renders a single-line status bar showing session state:
 * model, token usage, elapsed time, mode, cost.
 */

#include "nevercode.h"
#include "config.h"
#include "defaults.h"
#include "terminal.h"

typedef struct {
    char model[SBAR_MODEL_MAX];
    char mode[SBAR_MODE_MAX];
    int tokens_in;
    int tokens_out;
    double cost;
    int turns;
    int elapsed_s;
    bool active;
    bool dirty;
} sbar_state_t;

static sbar_state_t g_sbar;

void sbar_init(void) {
    memset(&g_sbar, 0, sizeof(g_sbar));
    nc_strlcpy(g_sbar.model, config_model(g_cfg_ptr), sizeof(g_sbar.model));
    nc_strlcpy(g_sbar.mode, SBAR_DEFAULT_MODE, sizeof(g_sbar.mode));
    g_sbar.dirty = true;
}

void sbar_free(void) {
    memset(&g_sbar, 0, sizeof(g_sbar));
}

void sbar_set_model(const char *m) {
    if (!m)
        return;
    nc_strlcpy(g_sbar.model, m, sizeof(g_sbar.model));
    g_sbar.dirty = true;
}

void sbar_set_mode(const char *m) {
    if (!m)
        return;
    nc_strlcpy(g_sbar.mode, m, sizeof(g_sbar.mode));
    g_sbar.dirty = true;
}

void sbar_set_tokens(int in, int out) {
    g_sbar.tokens_in = in;
    g_sbar.tokens_out = out;
    g_sbar.dirty = true;
}

void sbar_set_cost(double c) {
    g_sbar.cost = c;
    g_sbar.dirty = true;
}

void sbar_set_turns(int t) {
    g_sbar.turns = t;
    g_sbar.dirty = true;
}

void sbar_set_elapsed(int s) {
    g_sbar.elapsed_s = s;
    g_sbar.dirty = true;
}

void sbar_set_active(bool a) {
    g_sbar.active = a;
    g_sbar.dirty = true;
}

static void truncate_visible_with_ellipsis(const char *src, int max_vis, char *out, size_t out_sz) {
    int vis = line_visible_width(src);
    if (vis <= max_vis) {
        nc_strlcpy(out, src, out_sz);
        return;
    }
    if (max_vis <= 3) {
        size_t n = (size_t)(max_vis > 0 ? max_vis : 0);
        if (n >= out_sz)
            n = out_sz - 1;
        for (size_t i = 0; i < n; i++)
            out[i] = '.';
        out[n] = '\0';
        return;
    }
    line_copy_prefix_cells(src, max_vis - 3, out, out_sz);
    if (out_sz > 0) {
        size_t cur = strlen(out);
        if (cur + 3 < out_sz) {
            memcpy(out + cur, "...", 4);
        } else if (cur < out_sz) {
            size_t room = out_sz - cur - 1;
            if (room > 0) {
                memcpy(out + cur, "...", room);
                out[cur + room] = '\0';
            }
        }
    }
}

static void format_right_segment(char *right, size_t sz, int variant, const char *tin,
                                 const char *tout, const char *cost_s, const char *elapsed,
                                 int turns) {
    if (variant == 0) {
        snprintf(right, sz, "\u25b6 %s/%s \u2502 %s \u2502 %s \u2502 T%d ", tin, tout, cost_s,
                 elapsed, turns);
    } else if (variant == 1) {
        snprintf(right, sz, "\u25b6 %s/%s \u2502 T%d ", tin, tout, turns);
    } else {
        snprintf(right, sz, "T%d ", turns);
    }
}

char *sbar_render_for_width(int width) {
    int w = (width > 0) ? width : SBAR_MIN_TERM_WIDTH;
    strbuf_t sb;
    strbuf_init(&sb);

    char tin[NC_NUM_MAX], tout[NC_NUM_MAX], elapsed[NC_NUM_MAX];
    nc_fmt_tokens(tin, sizeof(tin), g_sbar.tokens_in);
    nc_fmt_tokens(tout, sizeof(tout), g_sbar.tokens_out);
    nc_fmt_elapsed_s(elapsed, sizeof(elapsed), g_sbar.elapsed_s);

    char cost_s[NC_NUM_MAX];
    if (g_sbar.cost >= 1.0)
        snprintf(cost_s, sizeof(cost_s), "$%.2f", g_sbar.cost);
    else if (g_sbar.cost >= 0.01)
        snprintf(cost_s, sizeof(cost_s), "$%.3f", g_sbar.cost);
    else
        snprintf(cost_s, sizeof(cost_s), "$%.4f", g_sbar.cost);

    const char *eg = effort_level_glyph(effort_level_get());
    char left_raw[SBAR_SEGMENT_MAX];
    snprintf(left_raw, sizeof(left_raw), " %s %s \u2502 %s", eg, g_sbar.model, g_sbar.mode);

    char right[SBAR_SEGMENT_MAX];
    int variant = 0;
    format_right_segment(right, sizeof(right), variant, tin, tout, cost_s, elapsed, g_sbar.turns);
    int right_len = line_visible_width(right);
    int left_budget = w - right_len - SBAR_PAD_MIN;
    if (left_budget < SBAR_LEFT_MIN_VISIBLE) {
        variant = 1;
        format_right_segment(right, sizeof(right), variant, tin, tout, cost_s, elapsed,
                             g_sbar.turns);
        right_len = line_visible_width(right);
        left_budget = w - right_len - SBAR_PAD_MIN;
    }
    if (left_budget < SBAR_LEFT_MIN_VISIBLE) {
        variant = 2;
        format_right_segment(right, sizeof(right), variant, tin, tout, cost_s, elapsed,
                             g_sbar.turns);
        right_len = line_visible_width(right);
        left_budget = w - right_len - SBAR_PAD_MIN;
    }
    if (left_budget < 1)
        left_budget = 1;

    char left[SBAR_SEGMENT_MAX];
    truncate_visible_with_ellipsis(left_raw, left_budget, left, sizeof(left));
    int left_len = line_visible_width(left);
    int pad = w - left_len - right_len;
    if (pad < SBAR_PAD_MIN)
        pad = SBAR_PAD_MIN;

    strbuf_append_str(&sb, T_REVERSE);
    strbuf_append_str(&sb, left);
    for (int i = 0; i < pad; i++)
        strbuf_append_str(&sb, " ");
    strbuf_append_str(&sb, right);
    strbuf_append_str(&sb, T_RESET);

    g_sbar.dirty = false;
    return strbuf_detach(&sb);
}

char *sbar_render(void) {
    return sbar_render_for_width(term_width());
}

bool sbar_is_dirty(void) {
    return g_sbar.dirty;
}

void sbar_print(void) {
    char *line = sbar_render();
    if (line) {
        fprintf(stderr, "\r%s", line);
        free(line);
    }
}

const char *sbar_get_model(void) {
    return g_sbar.model;
}
const char *sbar_get_mode(void) {
    return g_sbar.mode;
}
int sbar_get_turns(void) {
    return g_sbar.turns;
}
