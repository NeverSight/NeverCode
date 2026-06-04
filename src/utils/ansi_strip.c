/*
 * ansi_strip.c — ANSI escape code processing.
 *
 * Strips ANSI/VT100 escape sequences from terminal output to produce
 * clean plaintext. Handles CSI sequences, OSC sequences, and common
 * SGR (color/style) codes.
 */
#include "nevercode.h"
#include "defaults.h"

typedef enum { ST_NORMAL, ST_ESC, ST_CSI, ST_OSC } ansi_state_t;

int ansi_strip(const char *src, int src_len, char *out, int cap) {
    ansi_state_t state = ST_NORMAL;
    int w = 0;
    for (int i = 0; i < src_len && w < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (state) {
        case ST_NORMAL:
            if (c == 0x1B) {
                state = ST_ESC;
            } else if (c == 0x9B) {
                state = ST_CSI;
            } else {
                out[w++] = src[i];
            }
            break;
        case ST_ESC:
            if (c == '[') {
                state = ST_CSI;
            } else if (c == ']') {
                state = ST_OSC;
            } else if (c == '(' || c == ')') {
                state = ST_NORMAL;
            } else if (c >= 0x40 && c <= 0x7E) {
                state = ST_NORMAL;
            } else {
                state = ST_NORMAL;
            }
            break;
        case ST_CSI:
            if (c >= 0x40 && c <= 0x7E) {
                state = ST_NORMAL;
            }
            break;
        case ST_OSC:
            if (c == 0x07) {
                state = ST_NORMAL;
            } else if (c == 0x1B) {
                state = ST_ESC;
            }
            break;
        }
    }
    if (w < cap)
        out[w] = '\0';
    return w;
}

bool ansi_has_escapes(const char *src, int len) {
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == 0x1B || c == 0x9B)
            return true;
    }
    return false;
}

int ansi_visible_len(const char *src, int src_len) {
    int vis = 0;
    ansi_state_t state = ST_NORMAL;
    for (int i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (state) {
        case ST_NORMAL:
            if (c == 0x1B)
                state = ST_ESC;
            else if (c == 0x9B)
                state = ST_CSI;
            else if (c >= 0x20)
                vis++;
            break;
        case ST_ESC:
            if (c == '[')
                state = ST_CSI;
            else if (c == ']')
                state = ST_OSC;
            else
                state = ST_NORMAL;
            break;
        case ST_CSI:
            if (c >= 0x40 && c <= 0x7E)
                state = ST_NORMAL;
            break;
        case ST_OSC:
            if (c == 0x07 || c == 0x1B)
                state = ST_NORMAL;
            break;
        }
    }
    return vis;
}

int ansi_truncate_visible(const char *src, int src_len, int max_vis, char *out, int cap) {
    int vis = 0, w = 0;
    ansi_state_t state = ST_NORMAL;
    for (int i = 0; i < src_len && w < cap - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if (state == ST_NORMAL) {
            if (c == 0x1B) {
                state = ST_ESC;
                out[w++] = src[i];
            } else if (c == 0x9B) {
                state = ST_CSI;
                out[w++] = src[i];
            } else {
                if (vis >= max_vis)
                    break;
                out[w++] = src[i];
                if (c >= 0x20)
                    vis++;
            }
        } else {
            out[w++] = src[i];
            if (state == ST_ESC) {
                if (c == '[')
                    state = ST_CSI;
                else if (c == ']')
                    state = ST_OSC;
                else
                    state = ST_NORMAL;
            } else if (state == ST_CSI) {
                if (c >= 0x40 && c <= 0x7E)
                    state = ST_NORMAL;
            } else if (state == ST_OSC) {
                if (c == 0x07 || c == 0x1B)
                    state = ST_NORMAL;
            }
        }
    }
    if (w < cap)
        out[w] = '\0';
    return w;
}
