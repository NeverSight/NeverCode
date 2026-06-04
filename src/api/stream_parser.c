/*
 * stream_parser.c — UTF-8 streaming, citation stripping, and plan tag extraction.
 *
 * Provides utilities for processing streaming text responses:
 * - UTF-8 multi-byte reassembly across chunk boundaries
 * - CJK citation bracket (【…】) stripping
 * - <plan>…</plan> segment extraction
 * - Generic inline XML tag extraction and stripping
 */

#include "nevercode.h"
#include "defaults.h"

/* ── UTF-8 Streaming Reassembly ── */

typedef struct {
    unsigned char pending[4];
    int pending_len;
    int expected_len;
} utf8_stream_t;

void utf8_stream_init(utf8_stream_t *u) {
    memset(u, 0, sizeof(*u));
}

static int utf8_char_len(unsigned char c) {
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1;
}

int utf8_stream_feed(utf8_stream_t *u, const char *data, int len, char *out, int out_cap) {
    int w = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (u->pending_len == 0) {
            u->expected_len = utf8_char_len(c);
            u->pending[0] = c;
            u->pending_len = 1;
        } else {
            if (u->pending_len < 4)
                u->pending[u->pending_len++] = c;
        }
        if (u->pending_len >= u->expected_len) {
            if (w + u->pending_len <= out_cap) {
                memcpy(out + w, u->pending, u->pending_len);
                w += u->pending_len;
            }
            u->pending_len = 0;
            u->expected_len = 0;
        }
    }
    return w;
}

/* ── Citation Stripping ── */

int strip_citations(const char *src, int sl, char *out, int oc) {
    int w = 0, i = 0;
    while (i < sl && w < oc) {
        /* Detect CJK left bracket 【 (U+3010 = E3 80 90) */
        if (i + 2 < sl && (unsigned char)src[i] == 0xE3 && (unsigned char)src[i + 1] == 0x80 &&
            (unsigned char)src[i + 2] == 0x90) {
            int j = i + 3;
            bool f = false;
            /* Scan for right bracket 】 (U+3011 = E3 80 91) */
            while (j + 2 < sl) {
                if ((unsigned char)src[j] == 0xE3 && (unsigned char)src[j + 1] == 0x80 &&
                    (unsigned char)src[j + 2] == 0x91) {
                    f = true;
                    j += 3;
                    break;
                }
                j++;
            }
            if (f) {
                i = j;
                continue;
            }
        }
        out[w++] = src[i++];
    }
    return w;
}

/* ── Plan Segment Extraction ── */

typedef enum { PLAN_SEG_TEXT, PLAN_SEG_PLAN } plan_seg_type_t;

typedef struct {
    plan_seg_type_t type;
    const char *start;
    int len;
} plan_segment_t;

int extract_plan_segments(const char *src, int sl, plan_segment_t *sg, int cap) {
    int n = 0, pos = 0;
    while (pos < sl && n < cap) {
        /* Find next <plan> */
        const char *op = NULL;
        for (int i = pos; i + 6 <= sl; i++) {
            if (strncasecmp(src + i, "<plan>", 6) == 0) {
                op = src + i;
                break;
            }
        }

        if (!op) {
            if (pos < sl) {
                sg[n].type = PLAN_SEG_TEXT;
                sg[n].start = src + pos;
                sg[n].len = sl - pos;
                n++;
            }
            break;
        }

        int off = (int)(op - src);
        if (off > pos) {
            sg[n].type = PLAN_SEG_TEXT;
            sg[n].start = src + pos;
            sg[n].len = off - pos;
            n++;
            if (n >= cap)
                break;
        }

        int bd = off + 6;
        const char *cl = NULL;
        for (int i = bd; i + 7 <= sl; i++) {
            if (strncasecmp(src + i, "</plan>", 7) == 0) {
                cl = src + i;
                break;
            }
        }

        if (cl) {
            sg[n].type = PLAN_SEG_PLAN;
            sg[n].start = src + bd;
            sg[n].len = (int)(cl - (src + bd));
            n++;
            pos = (int)(cl - src) + 7;
        } else {
            sg[n].type = PLAN_SEG_PLAN;
            sg[n].start = src + bd;
            sg[n].len = sl - bd;
            n++;
            break;
        }
    }
    return n;
}

/* ── Inline XML Tag Extraction ── */

typedef struct {
    const char *tag_name;
    int tag_len;
} inline_tag_spec_t;

typedef struct {
    const char *content;
    int content_len;
    const char *tag_name;
} extracted_tag_t;

bool extract_inline_tag(const char *src, int sl, const inline_tag_spec_t *sp, extracted_tag_t *et) {
    char ob[NC_FMT_MAX], cb[NC_FMT_MAX];
    int ol = snprintf(ob, sizeof(ob), "<%s>", sp->tag_name);
    int cl = snprintf(cb, sizeof(cb), "</%s>", sp->tag_name);
    if (ol < 0 || cl < 0)
        return false;

    for (int i = 0; i + ol <= sl; i++) {
        if (strncmp(src + i, ob, ol) != 0)
            continue;
        int b = i + ol;
        for (int j = b; j + cl <= sl; j++) {
            if (strncmp(src + j, cb, cl) != 0)
                continue;
            et->content = src + b;
            et->content_len = j - b;
            et->tag_name = sp->tag_name;
            return true;
        }
        break;
    }
    return false;
}

int strip_inline_tags(const char *src, int sl, const inline_tag_spec_t *sp, char *out, int oc) {
    char ob[NC_FMT_MAX], cb[NC_FMT_MAX];
    int ol = snprintf(ob, sizeof(ob), "<%s>", sp->tag_name);
    int cl = snprintf(cb, sizeof(cb), "</%s>", sp->tag_name);
    if (ol < 0 || cl < 0) {
        int c = sl < oc ? sl : oc;
        memcpy(out, src, c);
        return c;
    }

    int w = 0, i = 0;
    while (i < sl && w < oc) {
        if (i + ol <= sl && strncmp(src + i, ob, ol) == 0) {
            int j = i + ol;
            bool f = false;
            while (j + cl <= sl) {
                if (strncmp(src + j, cb, cl) == 0) {
                    f = true;
                    j += cl;
                    break;
                }
                j++;
            }
            if (f) {
                i = j;
                continue;
            }
        }
        out[w++] = src[i++];
    }
    return w;
}
