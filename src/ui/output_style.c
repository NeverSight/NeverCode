#include "nevercode.h"
#include "defaults.h"

typedef enum { S_PLAIN = 0, S_RICH, S_JSON, S_STREAM, S_COMPACT } sty_t;

typedef struct {
    sty_t s;
    bool color, show_t, show_c;
    int w, renders;
} sc_t;

static sc_t gs;

void output_style_init(void) {
    memset(&gs, 0, sizeof(gs));
    gs.s = S_RICH;
    gs.color = true;
    gs.show_t = true;
    gs.show_c = true;
    gs.w = OSTYLE_WIDTH_DEFAULT;
}

void output_style_free(void) {
    memset(&gs, 0, sizeof(gs));
}
void output_style_set_color(bool on) {
    gs.color = on;
}
bool output_style_color(void) {
    return gs.color;
}

static void pal_open(strbuf_t *sb, int role) {
    if (gs.color)
        strbuf_append_str(sb, palette_seq(role));
}

static void pal_close(strbuf_t *sb) {
    if (gs.color)
        strbuf_append_str(sb, palette_reset());
}

char *output_style_header(const char *t) {
    strbuf_t sb;
    strbuf_init(&sb);
    gs.renders++;
    if (gs.s == S_RICH) {
        pal_open(&sb, PAL_ROLE_HEADER);
        strbuf_appendf(&sb, "=== %s ===", t);
        pal_close(&sb);
        strbuf_append_str(&sb, "\n");
    } else if (gs.s == S_JSON) {
        strbuf_appendf(&sb, "{\"type\":\"header\",\"title\":\"%s\"}\n", t);
    } else {
        strbuf_appendf(&sb, "--- %s ---\n", t);
    }
    return strbuf_detach(&sb);
}

char *output_style_tool_call(const char *n, const char *args) {
    strbuf_t sb;
    strbuf_init(&sb);
    gs.renders++;
    if (gs.s == S_RICH) {
        pal_open(&sb, PAL_ROLE_TOOL);
        strbuf_appendf(&sb, "> %s", n);
        pal_close(&sb);
        if (args) {
            pal_open(&sb, PAL_ROLE_MUTED);
            strbuf_appendf(&sb, " %s", args);
            pal_close(&sb);
        }
        strbuf_append_str(&sb, "\n");
    } else {
        strbuf_appendf(&sb, "TOOL: %s %s\n", n, args ? args : "");
    }
    return strbuf_detach(&sb);
}

char *output_style_text(const char *t) {
    strbuf_t sb;
    strbuf_init(&sb);
    gs.renders++;
    strbuf_append_str(&sb, t);
    return strbuf_detach(&sb);
}

char *output_style_footer(int ti, int to, double cost) {
    strbuf_t sb;
    strbuf_init(&sb);
    gs.renders++;
    if (gs.show_t)
        strbuf_appendf(&sb, "%d in / %d out", ti, to);
    if (gs.show_c)
        strbuf_appendf(&sb, "  $%.4f", cost);
    strbuf_append_str(&sb, "\n");
    return strbuf_detach(&sb);
}

char *output_style_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *n[] = {"plain", "rich", "json", "streaming", "compact"};
    strbuf_appendf(&sb, "Output Style: %s, color=%s, width=%d, renders=%d", n[gs.s],
                   gs.color ? "yes" : "no", gs.w, gs.renders);
    return strbuf_detach(&sb);
}
