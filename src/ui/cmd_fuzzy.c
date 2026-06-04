/*
 * cmd_fuzzy.c — Fuzzy matching for slash commands.
 *
 * When the user types a partial or misspelled slash command, returns
 * ranked suggestions using edit-distance scoring.  Matches command
 * names, descriptions, and aliases.
 */

#include "nevercode.h"

typedef struct {
    const char *name;
    int score;
} cmd_match_t;

static int edit_distance(const char *a, const char *b) {
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    if (la > CMD_FUZZY_MAX_LEN || lb > CMD_FUZZY_MAX_LEN)
        return CMD_FUZZY_MAX_LEN;

    int prev[CMD_FUZZY_MAX_LEN + 1], curr[CMD_FUZZY_MAX_LEN + 1];
    for (int j = 0; j <= lb; j++)
        prev[j] = j;

    for (int i = 1; i <= la; i++) {
        curr[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost =
                (tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 1])) ? 0 : 1;
            int ins = curr[j - 1] + 1;
            int del = prev[j] + 1;
            int sub = prev[j - 1] + cost;
            curr[j] = ins < del ? (ins < sub ? ins : sub) : (del < sub ? del : sub);
        }
        memcpy(prev, curr, sizeof(int) * (lb + 1));
    }
    return prev[lb];
}

static int prefix_bonus(const char *cmd, const char *query) {
    int len = (int)strlen(query);
    for (int i = 0; i < len; i++) {
        if (tolower((unsigned char)cmd[i]) != tolower((unsigned char)query[i]))
            return 0;
    }
    return CMD_FUZZY_PREFIX_BONUS;
}

static int contains_bonus(const char *cmd, const char *query) {
    int qlen = (int)strlen(query);
    int clen = (int)strlen(cmd);
    if (qlen > clen)
        return 0;
    for (int i = 0; i <= clen - qlen; i++) {
        bool match = true;
        for (int j = 0; j < qlen && match; j++) {
            if (tolower((unsigned char)cmd[i + j]) != tolower((unsigned char)query[j]))
                match = false;
        }
        if (match)
            return CMD_FUZZY_CONTAINS_BONUS;
    }
    return 0;
}

int cmd_fuzzy_match(const char *query, const char **commands, int ncmds, cmd_fuzzy_result_t *out,
                    int max_results) {
    if (!query || !commands || !out || ncmds <= 0 || max_results <= 0)
        return 0;

    /* Skip leading / */
    if (query[0] == '/')
        query++;
    if (!query[0])
        return 0;

    cmd_match_t matches[CMD_FUZZY_MAX_CANDIDATES];
    int nm = 0;

    for (int i = 0; i < ncmds && nm < CMD_FUZZY_MAX_CANDIDATES; i++) {
        const char *cmd = commands[i];
        if (!cmd || !cmd[0])
            continue;

        /* Skip leading / in stored command name */
        const char *cname = cmd;
        if (cname[0] == '/')
            cname++;

        int dist = edit_distance(cname, query);
        int threshold = (int)strlen(query) / 2 + 1;
        if (threshold < CMD_FUZZY_THRESHOLD)
            threshold = CMD_FUZZY_THRESHOLD;

        int bonus = prefix_bonus(cname, query) + contains_bonus(cname, query);

        if (dist <= threshold || bonus > 0) {
            matches[nm].name = cmd;
            matches[nm].score = 1000 - dist * 100 + bonus;
            nm++;
        }
    }

    /* Sort by score descending */
    for (int i = 0; i < nm - 1; i++) {
        for (int j = i + 1; j < nm; j++) {
            if (matches[j].score > matches[i].score) {
                cmd_match_t tmp = matches[i];
                matches[i] = matches[j];
                matches[j] = tmp;
            }
        }
    }

    int count = NC_MIN(nm, max_results);
    for (int i = 0; i < count; i++) {
        nc_strlcpy(out[i].name, matches[i].name, sizeof(out[i].name));
        out[i].score = matches[i].score;
    }
    return count;
}
