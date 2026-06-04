/*
 * commit_attr.c — Commit attribution trailer generation
 *
 * Ensures git commits include a Co-authored-by trailer to attribute
 * AI-assisted work.
 */

#include "nevercode.h"

/* NC_COMMIT_ATTRIBUTION in defaults.h */
#define DEFAULT_ATTRIBUTION NC_COMMIT_ATTRIBUTION

static char g_attribution[NC_MSG_MAX] = "";

void commit_attr_init(const char *attribution) {
    if (attribution && *attribution) {
        nc_strlcpy(g_attribution, attribution, sizeof(g_attribution));
    } else {
        nc_strlcpy(g_attribution, DEFAULT_ATTRIBUTION, sizeof(g_attribution));
    }
}

const char *commit_attr_value(void) {
    if (!g_attribution[0])
        commit_attr_init(NULL);
    return g_attribution;
}

char *commit_attr_trailer(void) {
    const char *val = commit_attr_value();
    if (!val || !*val)
        return NULL;

    size_t len = strlen("Co-authored-by: ") + strlen(val) + 1;
    char *out = malloc(len);
    if (!out)
        return NULL;
    snprintf(out, len, "Co-authored-by: %s", val);
    return out;
}

char *commit_attr_instruction(void) {
    char *trailer = commit_attr_trailer();
    if (!trailer)
        return NULL;

    const char *fmt = "When you write or edit a git commit message, ensure the message "
                      "ends with this trailer exactly once:\n%s\n\n"
                      "Rules:\n"
                      "- Keep existing trailers and append this trailer at the end if missing.\n"
                      "- Do not duplicate this trailer if it already exists.\n"
                      "- Keep one blank line between the commit body and trailer block.";

    size_t len = strlen(fmt) + strlen(trailer) + 1;
    char *out = malloc(len);
    if (!out) {
        free(trailer);
        return NULL;
    }
    snprintf(out, len, fmt, trailer);
    free(trailer);
    return out;
}

bool commit_msg_has_trailer(const char *msg) {
    if (!msg)
        return false;
    char *trailer = commit_attr_trailer();
    if (!trailer)
        return false;
    bool found = strstr(msg, trailer) != NULL;
    free(trailer);
    return found;
}

char *commit_msg_ensure_trailer(const char *msg) {
    if (!msg)
        return NULL;
    if (commit_msg_has_trailer(msg))
        return nc_xstrdup(msg);

    char *trailer = commit_attr_trailer();
    if (!trailer)
        return nc_xstrdup(msg);

    size_t msg_len = strlen(msg);
    size_t trailer_len = strlen(trailer);

    while (msg_len > 0 && msg[msg_len - 1] == '\n')
        msg_len--;

    size_t len = msg_len + 2 + trailer_len + 2;
    char *out = malloc(len);
    if (!out) {
        free(trailer);
        return nc_xstrdup(msg);
    }
    snprintf(out, len, "%.*s\n\n%s\n", (int)msg_len, msg, trailer);
    free(trailer);
    return out;
}
