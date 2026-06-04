/*
 * transcript_search.c — Search across session transcripts
 *
 * Provides full-text search across saved session transcripts to
 * find relevant context from past sessions. Supports keyword
 * matching, time-range filtering, and relevance scoring.
 */

#include "nevercode.h"

/* TS_MAX_RESULTS, TS_SNIPPET_CHARS, TS_MAX_KEYWORDS in defaults.h */

typedef struct {
    char session_id[NC_TAG_MAX];
    char snippet[TS_SNIPPET_CHARS + 1];
    int score;
    int line_number;
    time_t timestamp;
} ts_result_t;

typedef struct {
    ts_result_t results[TS_MAX_RESULTS];
    int count;
    int total_scanned;
    int total_matched;
} ts_search_t;

static int keyword_count_in(const char *text, const char *keyword) {
    if (!text || !keyword || !keyword[0])
        return 0;
    int count = 0;
    size_t klen = strlen(keyword);
    const char *p = text;
    while ((p = strcasestr(p, keyword)) != NULL) {
        count++;
        p += klen;
    }
    return count;
}

static void extract_snippet(const char *text, const char *keyword, char *out, int maxlen) {
    const char *pos = strcasestr(text, keyword);
    if (!pos)
        pos = text;

    int offset = (int)(pos - text);
    int start = offset - maxlen / 2;
    if (start < 0)
        start = 0;

    int tlen = (int)strlen(text);
    int end = start + maxlen;
    if (end > tlen)
        end = tlen;

    int slen = end - start;
    if (slen > maxlen)
        slen = maxlen;
    memcpy(out, text + start, (size_t)slen);
    out[slen] = '\0';
}

static int scan_file(const char *path, const char *session_id, const char **keywords, int nkw,
                     ts_search_t *search) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    char line[NC_PATH_MAX];
    int lineno = 0;
    int matched = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        int score = 0;
        for (int k = 0; k < nkw; k++)
            score += keyword_count_in(line, keywords[k]);

        if (score > 0 && search->count < TS_MAX_RESULTS) {
            ts_result_t *r = &search->results[search->count];
            nc_strlcpy(r->session_id, session_id, sizeof(r->session_id));
            extract_snippet(line, keywords[0], r->snippet, TS_SNIPPET_CHARS);
            r->score = score;
            r->line_number = lineno;
            r->timestamp = 0;
            search->count++;
            matched++;
        }
    }

    fclose(f);
    return matched;
}

int transcript_search(const char *home, const char *query, ts_search_t *search) {
    if (!home || !query || !search)
        return -1;

    memset(search, 0, sizeof(*search));

    const char *keywords[TS_MAX_KEYWORDS];
    int nkw = 0;

    char *qcopy = nc_xstrdup(query);
    char *tok = strtok(qcopy, " \t");
    while (tok && nkw < TS_MAX_KEYWORDS) {
        keywords[nkw++] = tok;
        tok = strtok(NULL, " \t");
    }

    if (nkw == 0) {
        free(qcopy);
        return 0;
    }

    char sdir[NC_PATH_MAX];
    snprintf(sdir, sizeof(sdir), "%s/sessions", home);

    DIR *d = opendir(sdir);
    if (!d) {
        free(qcopy);
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;

        char fpath[NC_PATH_MAX];
        snprintf(fpath, sizeof(fpath), "%s/%s", sdir, ent->d_name);

        struct stat st;
        if (stat(fpath, &st) != 0)
            continue;

        search->total_scanned++;

        if (S_ISREG(st.st_mode)) {
            int m = scan_file(fpath, ent->d_name, keywords, nkw, search);
            if (m > 0)
                search->total_matched++;
        } else if (S_ISDIR(st.st_mode)) {
            char tpath[NC_PATH_MAX];
            snprintf(tpath, sizeof(tpath), "%s/transcript.jsonl", fpath);
            int m = scan_file(tpath, ent->d_name, keywords, nkw, search);
            if (m > 0)
                search->total_matched++;
        }
    }

    closedir(d);
    free(qcopy);

    /* sort by score descending */
    for (int i = 0; i < search->count - 1; i++)
        for (int j = i + 1; j < search->count; j++)
            if (search->results[j].score > search->results[i].score) {
                ts_result_t tmp = search->results[i];
                search->results[i] = search->results[j];
                search->results[j] = tmp;
            }

    return search->count;
}

char *transcript_search_format(const ts_search_t *search) {
    if (!search)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Found %d results across %d/%d sessions:\n", search->count,
                   search->total_matched, search->total_scanned);
    for (int i = 0; i < search->count; i++) {
        const ts_result_t *r = &search->results[i];
        strbuf_appendf(&sb, "  [%d] session=%s line=%d score=%d\n    %s\n", i + 1, r->session_id,
                       r->line_number, r->score, r->snippet);
    }
    return strbuf_detach(&sb);
}
