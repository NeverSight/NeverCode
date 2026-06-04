/*
 * arg_subst.c — $ARGUMENTS placeholder substitution for skill prompts.
 *
 * Replaces $ARGUMENTS (full string), $ARGUMENTS[N] / $N (indexed),
 * and $name (named via frontmatter) with actual user-supplied values.
 */

#include "nevercode.h"

static int split_args(const char *input, char **out, int max) {
    if (!input)
        return 0;
    int count = 0;
    const char *p = input;

    while (*p && count < max) {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;

        const char *start;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            start = p;
            while (*p && *p != q)
                p++;
            int len = (int)(p - start);
            out[count] = malloc(len + 1);
            if (!out[count])
                break;
            memcpy(out[count], start, len);
            out[count][len] = '\0';
            count++;
            if (*p == q)
                p++;
        } else {
            start = p;
            while (*p && !isspace((unsigned char)*p))
                p++;
            int len = (int)(p - start);
            out[count] = malloc(len + 1);
            if (!out[count])
                break;
            memcpy(out[count], start, len);
            out[count][len] = '\0';
            count++;
        }
    }
    return count;
}

char *arg_subst_expand(const char *tmpl, const char *args_str, const char **named_keys,
                       int num_names) {
    if (!tmpl)
        return NULL;
    if (!args_str)
        args_str = "";

    char *argv[ARG_SUBST_MAX_ARGS];
    int argc = split_args(args_str, argv, ARG_SUBST_MAX_ARGS);

    strbuf_t sb;
    strbuf_init(&sb);

    const char *p = tmpl;
    while (*p) {
        if (*p != '$') {
            strbuf_append(&sb, p, 1);
            p++;
            continue;
        }

        /* $ARGUMENTS[N] */
        if (nc_starts_with(p, "$ARGUMENTS[")) {
            const char *nb = p + 11;
            char *end;
            long idx = strtol(nb, &end, 10);
            if (*end == ']') {
                if (idx >= 0 && idx < argc)
                    strbuf_append_str(&sb, argv[idx]);
                p = end + 1;
                continue;
            }
        }

        /* $ARGUMENTS (full) */
        if (nc_starts_with(p, "$ARGUMENTS") && (p[10] == '\0' || !isalnum((unsigned char)p[10]))) {
            strbuf_append_str(&sb, args_str);
            p += 10;
            continue;
        }

        /* $N shorthand */
        if (isdigit((unsigned char)p[1])) {
            char *end;
            long idx = strtol(p + 1, &end, 10);
            if (end > p + 1 && (*end == '\0' || !isalnum((unsigned char)*end))) {
                if (idx >= 0 && idx < argc)
                    strbuf_append_str(&sb, argv[idx]);
                p = end;
                continue;
            }
        }

        /* $name (named argument) */
        if (isalpha((unsigned char)p[1]) || p[1] == '_') {
            const char *ns = p + 1;
            const char *ne = ns;
            while (*ne && (isalnum((unsigned char)*ne) || *ne == '_'))
                ne++;
            int nlen = (int)(ne - ns);
            bool found = false;
            for (int i = 0; i < num_names && i < argc; i++) {
                if (named_keys && named_keys[i] && (int)strlen(named_keys[i]) == nlen &&
                    strncmp(named_keys[i], ns, nlen) == 0) {
                    strbuf_append_str(&sb, argv[i]);
                    found = true;
                    break;
                }
            }
            if (found) {
                p = ne;
                continue;
            }
        }

        strbuf_append(&sb, p, 1);
        p++;
    }

    for (int i = 0; i < argc; i++)
        free(argv[i]);

    return strbuf_detach(&sb);
}
