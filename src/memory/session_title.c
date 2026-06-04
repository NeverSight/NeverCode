/*
 * session_title.c — Automatic session title extraction from first user message.
 *
 * Generates a concise, human-readable title (3-8 words) from the first
 * user message in a conversation.  Pure heuristic — no API call required.
 */

#include "nevercode.h"

/* NC_SESSION_TITLE_MAX, NC_SESSION_TITLE_WORDS in defaults.h */

static int is_sentence_end(char c) {
    return c == '.' || c == '!' || c == '?' || c == '\n';
}

/*
 * Extract a title from raw user text.
 *
 *  1. Skip leading whitespace / markdown noise.
 *  2. Take up to the first sentence boundary or NC_SESSION_TITLE_WORDS words.
 *  3. Truncate at NC_SESSION_TITLE_MAX characters.
 *  4. Capitalise first letter.
 */
char *session_title_extract(const char *user_msg) {
    if (!user_msg || !user_msg[0])
        return NULL;

    const char *p = nc_skip_ws(user_msg);

    /* Skip markdown headers */
    while (*p == '#' || *p == ' ')
        p++;

    /* Skip common prefixes that don't add info */
    static const char *noise[] = {
        "please ",    "can you ",       "i want to ",   "i need to ",     "help me ",
        "could you ", "would you ",     "i'd like to ", "let's ",         "go ahead and ",
        "just ",      "i want you to ", "you should ",  "i need you to ", "make sure to ",
        "try to ",    "let me ",        "okay "};
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NC_ARRAY_LEN(noise); i++) {
            size_t nlen = strlen(noise[i]);
            if (strncasecmp(p, noise[i], nlen) == 0) {
                p = nc_skip_ws(p + nlen);
                break;
            }
        }
    }

    char buf[NC_SESSION_TITLE_MAX + 4];
    int bi = 0, words = 0;
    bool in_word = false;

    while (*p && bi < NC_SESSION_TITLE_MAX && words < NC_SESSION_TITLE_WORDS) {
        if (is_sentence_end(*p))
            break;

        if (isspace((unsigned char)*p)) {
            if (in_word) {
                words++;
                in_word = false;
                if (words >= NC_SESSION_TITLE_WORDS)
                    break;
            }
            buf[bi++] = ' ';
            p++;
            while (*p && isspace((unsigned char)*p))
                p++;
        } else {
            buf[bi++] = *p++;
            in_word = true;
        }
    }

    while (bi > 0 && isspace((unsigned char)buf[bi - 1]))
        bi--;
    buf[bi] = '\0';

    if (bi == 0)
        return NULL;

    if (bi >= NC_SESSION_TITLE_MAX - 3) {
        while (bi > 0 && !isspace((unsigned char)buf[bi - 1]))
            bi--;
        if (bi > 0) {
            while (bi > 0 && isspace((unsigned char)buf[bi - 1]))
                bi--;
        }
        buf[bi++] = '.';
        buf[bi++] = '.';
        buf[bi++] = '.';
        buf[bi] = '\0';
    }

    buf[0] = (char)toupper((unsigned char)buf[0]);
    return nc_xstrdup(buf);
}

int session_title_auto(const char *session_id, const char *user_msg) {
    if (!session_id || !user_msg)
        return -1;

    char *title = session_title_extract(user_msg);
    if (!title)
        return -1;

    int rc = history_rename(session_id, title);
    free(title);
    return rc;
}
