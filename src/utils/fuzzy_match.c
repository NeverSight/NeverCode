#include "nevercode.h"

/*
 * Case-insensitive subsequence fuzzy matcher.
 * Returns matched char indices + score (lower = better).
 * Scoring bonuses: contiguous match, word-boundary match, prefix match.
 */

int fuzzy_match(const char *haystack, const char *needle, int *indices, int max_indices,
                int *score_out) {
    if (!haystack || !needle)
        return 0;
    if (!*needle) {
        if (score_out)
            *score_out = 0x7FFFFFFF;
        return 0;
    }

    int hlen = (int)strlen(haystack);
    int nlen = (int)strlen(needle);
    if (nlen > hlen)
        return 0;

    int matched = 0;
    int hi = 0, ni = 0;
    int score = 0;
    int last_match = -2;

    while (hi < hlen && ni < nlen) {
        char hc = (char)tolower((unsigned char)haystack[hi]);
        char nc = (char)tolower((unsigned char)needle[ni]);

        if (hc == nc) {
            if (indices && matched < max_indices)
                indices[matched] = hi;

            if (last_match == hi - 1)
                score -= 5; /* contiguous bonus */
            else
                score += 1; /* gap penalty per non-contiguous */

            if (hi == 0)
                score -= 10; /* prefix bonus */

            if (hi > 0) {
                char prev = haystack[hi - 1];
                if (prev == '_' || prev == '-' || prev == '/' || prev == '.' ||
                    (islower((unsigned char)prev) && isupper((unsigned char)haystack[hi])))
                    score -= 3; /* word-boundary bonus */
            }

            last_match = hi;
            matched++;
            ni++;
        }
        hi++;
    }

    if (ni < nlen)
        return 0; /* needle not fully matched */

    if (score_out)
        *score_out = score;
    return matched;
}

int fuzzy_score(const char *haystack, const char *needle) {
    int score = 0;
    int rc = fuzzy_match(haystack, needle, NULL, 0, &score);
    return rc > 0 ? score : 0x7FFFFFFF;
}

int fuzzy_filter(const char **items, int count, const char *query, fuzzy_result_t *results,
                 int max_results) {
    if (!items || !query || !results || max_results <= 0)
        return 0;

    int num = 0;
    for (int i = 0; i < count && num < max_results; i++) {
        if (!items[i])
            continue;
        int score = 0;
        int matched = fuzzy_match(items[i], query, NULL, 0, &score);
        if (matched > 0) {
            results[num].item = items[i];
            results[num].index = i;
            results[num].score = score;
            num++;
        }
    }

    /* Sort by score ascending (lower = better match) */
    for (int i = 0; i < num - 1; i++)
        for (int j = i + 1; j < num; j++)
            if (results[j].score < results[i].score) {
                fuzzy_result_t tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }

    return num;
}
