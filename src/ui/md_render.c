/*
 * md_render.c — markdown → ANSI terminal text renderer
 *
 * Handles: headers (#, ##, ###), bold (**), italic (*), strikethrough (~~),
 * inline code (`), fenced code blocks (```), links, bullet/numbered lists,
 * nested bullets, horizontal rules, and blockquotes (>).
 */

#include "nevercode.h"
#include "terminal.h"

#define is_cjk_punct nc_is_cjk_punct

static void strbuf_code_tokens(strbuf_t *sb, const char *code, size_t code_len, const char *color) {
    size_t end = code_len;
    for (;;) {
        if (end > 0 && strchr(".,;:!?", code[end - 1]))
            end--;
        else if (end >= 3 && is_cjk_punct(code + end - 3, 3))
            end -= 3;
        else
            break;
    }
    size_t k = 0;
    while (k < end) {
        size_t sep;
        if (code[k] == ' ') {
            strbuf_append(sb, " ", 1);
            k++;
        } else if ((sep = is_cjk_punct(code + k, end - k)) > 0) {
            strbuf_append(sb, code + k, sep);
            k += sep;
        } else {
            size_t ts = k;
            while (k < end && code[k] != ' ' && is_cjk_punct(code + k, end - k) == 0)
                k++;
            size_t tl = k - ts;
            strbuf_append_str(sb, color);
            strbuf_append(sb, code + ts, tl);
            strbuf_append_str(sb, T_RESET);
        }
    }
    if (end < code_len)
        strbuf_append(sb, code + end, code_len - end);
}

static void render_inline(strbuf_t *sb, const char *text, size_t len) {
    size_t i = 0;
    while (i < len) {
        /* inline code: `...` */
        if (text[i] == '`') {
            size_t j = i + 1;
            while (j < len && text[j] != '`')
                j++;
            if (j < len) {
                strbuf_code_tokens(sb, text + i + 1, j - i - 1, T_GREEN);
                i = j + 1;
                continue;
            }
        }
        /* strikethrough: ~~...~~ */
        if (i + 1 < len && text[i] == '~' && text[i + 1] == '~') {
            size_t j = i + 2;
            while (j + 1 < len && !(text[j] == '~' && text[j + 1] == '~'))
                j++;
            if (j + 1 < len) {
                strbuf_append_str(sb, T_STRIKE);
                render_inline(sb, text + i + 2, j - i - 2);
                strbuf_append_str(sb, T_RESET);
                i = j + 2;
                continue;
            }
        }
        /* bold: **...** */
        if (i + 1 < len && text[i] == '*' && text[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < len && !(text[j] == '*' && text[j + 1] == '*'))
                j++;
            if (j + 1 < len) {
                strbuf_append_str(sb, T_BOLD);
                render_inline(sb, text + i + 2, j - i - 2);
                strbuf_append_str(sb, T_RESET);
                i = j + 2;
                continue;
            }
        }
        /* italic: *...* */
        if (text[i] == '*' && (i + 1 < len) && text[i + 1] != '*') {
            size_t j = i + 1;
            while (j < len && text[j] != '*')
                j++;
            if (j < len) {
                strbuf_append_str(sb, T_ITALIC);
                render_inline(sb, text + i + 1, j - i - 1);
                strbuf_append_str(sb, T_RESET);
                i = j + 1;
                continue;
            }
        }
        /* link: [text](url) — no underline; let terminal own link styling */
        if (text[i] == '[') {
            size_t close = i + 1;
            while (close < len && text[close] != ']')
                close++;
            if (close + 1 < len && text[close + 1] == '(') {
                size_t paren = close + 2;
                while (paren < len && text[paren] != ')')
                    paren++;
                if (paren < len) {
                    strbuf_append_str(sb, T_CYAN);
                    strbuf_append(sb, text + i + 1, close - i - 1);
                    strbuf_append_str(sb, T_RESET);
                    strbuf_append_str(sb, T_DIM " (");
                    strbuf_append(sb, text + close + 2, paren - close - 2);
                    strbuf_append_str(sb, ")" T_RESET);
                    i = paren + 1;
                    continue;
                }
            }
        }
        strbuf_append(sb, text + i, 1);
        i++;
    }
}

char *md_render_to_terminal(const char *markdown) {
    if (!markdown)
        return NULL;

    strbuf_t out;
    strbuf_init(&out);
    const char *p = markdown;
    bool in_code_block = false;

    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
        const char *raw_line = p;
        size_t raw_ll = linelen;

        if (linelen >= 3 && memcmp(p, "```", 3) == 0) {
            in_code_block = !in_code_block;
            if (in_code_block)
                strbuf_append_str(&out, T_DIM "──────────\n");
            else
                strbuf_append_str(&out, "──────────" T_RESET "\n");
            p = nl ? nl + 1 : p + linelen;
            continue;
        }

        if (in_code_block) {
            strbuf_append_str(&out, T_GREEN);
            strbuf_append(&out, p, linelen);
            strbuf_append_str(&out, T_RESET "\n");
            p = nl ? nl + 1 : p + linelen;
            continue;
        }

        const char *line = p;
        size_t ll = linelen;
        while (ll > 0 && isspace((unsigned char)line[0])) {
            line++;
            ll--;
        }

        if (ll == 0) {
            strbuf_append_str(&out, "\n");
        } else if (ll >= 3 && line[0] == '#' && line[1] == '#' && line[2] == '#') {
            strbuf_append_str(&out, T_BOLD T_CYAN "### ");
            const char *h = line + 3;
            size_t hl = ll - 3;
            while (hl > 0 && *h == ' ') {
                h++;
                hl--;
            }
            render_inline(&out, h, hl);
            strbuf_append_str(&out, T_RESET "\n");
        } else if (ll >= 2 && line[0] == '#' && line[1] == '#') {
            strbuf_append_str(&out, T_BOLD T_CYAN "## ");
            const char *h = line + 2;
            size_t hl = ll - 2;
            while (hl > 0 && *h == ' ') {
                h++;
                hl--;
            }
            render_inline(&out, h, hl);
            strbuf_append_str(&out, T_RESET "\n");
        } else if (ll >= 1 && line[0] == '#') {
            strbuf_append_str(&out, T_BOLD T_CYAN);
            const char *h = line + 1;
            size_t hl = ll - 1;
            while (hl > 0 && *h == ' ') {
                h++;
                hl--;
            }
            render_inline(&out, h, hl);
            strbuf_append_str(&out, T_RESET "\n");
        } else if (raw_ll >= 4 && (raw_line[0] == ' ' || raw_line[0] == '\t') &&
                   ((raw_line[0] == ' ' && raw_ll >= 4 &&
                     (raw_line[2] == '-' || raw_line[2] == '*') && raw_line[3] == ' ') ||
                    (raw_line[0] == '\t' && raw_ll >= 3 &&
                     (raw_line[1] == '-' || raw_line[1] == '*') && raw_line[2] == ' '))) {
            /* Nested list (indented - or *) */
            const char *body = raw_line;
            while (*body == ' ' || *body == '\t')
                body++;
            size_t remain = raw_ll - (size_t)(body - raw_line);
            if (remain >= 2 && (body[0] == '-' || body[0] == '*') && body[1] == ' ') {
                strbuf_append_str(&out, "    ◦ ");
                render_inline(&out, body + 2, remain - 2);
                strbuf_append_str(&out, "\n");
            } else {
                render_inline(&out, raw_line, raw_ll);
                strbuf_append_str(&out, "\n");
            }
        } else if ((ll >= 2 && line[0] == '-' && line[1] == ' ') ||
                   (ll >= 2 && line[0] == '*' && line[1] == ' ')) {
            strbuf_append_str(&out, "  • ");
            render_inline(&out, line + 2, ll - 2);
            strbuf_append_str(&out, "\n");
        } else if (ll >= 2 && isdigit((unsigned char)line[0])) {
            /* Numbered list: 1. / 2. / 10. etc. */
            const char *dot = line;
            while (dot < line + ll && isdigit((unsigned char)*dot))
                dot++;
            if (dot < line + ll && *dot == '.' && dot + 1 < line + ll && *(dot + 1) == ' ') {
                strbuf_append_str(&out, "  ");
                strbuf_append(&out, line, (size_t)(dot - line + 1));
                strbuf_append_str(&out, " ");
                const char *body = dot + 2;
                size_t remain = ll - (size_t)(body - line);
                render_inline(&out, body, remain);
                strbuf_append_str(&out, "\n");
            } else {
                render_inline(&out, line, ll);
                strbuf_append_str(&out, "\n");
            }
        } else if (ll >= 2 && line[0] == '>' && line[1] == ' ') {
            strbuf_append_str(&out, T_DIM "│ ");
            render_inline(&out, line + 2, ll - 2);
            strbuf_append_str(&out, T_RESET "\n");
        } else if (ll >= 3 && line[0] == '-' && line[1] == '-' && line[2] == '-') {
            strbuf_append_str(&out, T_DIM "────────────────────" T_RESET "\n");
        } else {
            render_inline(&out, line, ll);
            strbuf_append_str(&out, "\n");
        }

        if (nl) {
            p = nl + 1;
        } else
            break;
    }

    if (in_code_block)
        strbuf_append_str(&out, "──────────" T_RESET "\n");

    char *raw = strbuf_detach(&out);
    if (!raw)
        return nc_xstrdup("");
    int tw = term_width();
    if (tw < 20)
        tw = LWRAP_DEFAULT_WIDTH;
    char *wrapped = line_wrap(raw, tw);
    free(raw);
    return wrapped ? wrapped : nc_xstrdup("");
}

int md_render_line_count(const char *markdown) {
    if (!markdown || !markdown[0])
        return 0;
    int n = 1;
    for (const char *p = markdown; *p; p++)
        if (*p == '\n')
            n++;
    return n;
}
