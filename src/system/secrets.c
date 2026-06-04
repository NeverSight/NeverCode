/*
 * secrets.c — Secret / credential detection and redaction
 *
 * Pattern-based scanner that detects API keys, tokens, and other
 * credentials in text.  Supports an allow-list so known-safe values
 * pass through without redaction.  The redact function masks detected
 * secrets while preserving prefix characters for debuggability.
 */

#include "nevercode.h"
#include "defaults.h"

typedef struct {
    const char *label;
    const char *prefix;
    int prefix_len;
    int min_key_len;
} secret_pattern_t;

static const secret_pattern_t patterns[] = {
    {"api-key-sk", "sk-", 3, 20},
    {"bearer-token", "Bearer ", 7, 20},
    {"ghp-token", "ghp_", 4, 30},
    {"gho-token", "gho_", 4, 30},
    {"ghu-token", "ghu_", 4, 30},
    {"ghs-token", "ghs_", 4, 30},
    {"ghr-token", "ghr_", 4, 30},
    {"github-pat", "github_pat_", 11, 20},
    {"aws-key", "AKIA", 4, 16},
    {"npm-token", "npm_", 4, 20},
    {"slack-xoxb", "xoxb-", 5, 20},
    {"slack-xoxp", "xoxp-", 5, 20},
    {"slack-xoxa", "xoxa-", 5, 20},
    {"slack-xoxr", "xoxr-", 5, 20},
    {"stripe-sk-live", "sk_live_", 8, 16},
    {"stripe-sk-test", "sk_test_", 8, 16},
    {"stripe-pk-live", "pk_live_", 8, 16},
    {"stripe-rk-live", "rk_live_", 8, 16},
    {"sendgrid", "SG.", 3, 20},
    {"google-api", "AIza", 4, 30},
    {"hf-token", "hf_", 3, 10},
    {"replicate", "r8_", 3, 10},
    {"pypi", "pypi-", 5, 10},
    {"do-pat", "dop_v1_", 7, 10},
    {"do-oauth", "doo_v1_", 7, 10},
    {"perplexity", "pplx-", 5, 10},
    {"firecrawl", "fc-", 3, 10},
    {"fal-ai", "fal_", 4, 10},
    {"browserbase", "bb_live_", 8, 10},
    {"agentmail", "am_", 3, 10},
    {"tavily", "tvly-", 5, 10},
    {"exa-search", "exa_", 4, 10},
    {"elevenlabs", "xi_", 3, 10},
    {"ssh-private", "-----BEGIN", 10, 30},
    {0, 0, 0, 0},
};

/* DB connection string protocol prefixes for password redaction */
static const char *const DB_PROTOS[] = {"postgres://",    "postgresql://", "mysql://", "mongodb://",
                                        "mongodb+srv://", "redis://",      "amqp://",  NULL};

static char *g_allowed[SECRETS_MAX_ALLOWED];
static int g_allowed_count = 0;
static bool g_initialized = false;
static bool g_redact_enabled = true;

static bool is_key_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '/' || c == '+' || c == '=' ||
           c == '.';
}

static int key_length(const char *s) {
    int n = 0;
    while (is_key_char(s[n]))
        n++;
    return n;
}

static bool is_allowed(const char *candidate, int len) {
    for (int i = 0; i < g_allowed_count; i++)
        if ((int)strlen(g_allowed[i]) == len && memcmp(g_allowed[i], candidate, (size_t)len) == 0)
            return true;
    return false;
}

void secrets_init(void) {
    memset(g_allowed, 0, sizeof(g_allowed));
    g_allowed_count = 0;
    g_initialized = true;
    g_redact_enabled = true;
    const char *env = getenv(ENV_REDACT_SECRETS);
    if (env && !nc_parse_bool(env, true))
        g_redact_enabled = false;
}

void secrets_free(void) {
    if (!g_initialized)
        return;
    for (int i = 0; i < g_allowed_count; i++)
        free(g_allowed[i]);
    g_allowed_count = 0;
    g_initialized = false;
}

int secrets_allow(const char *value) {
    if (!value || g_allowed_count >= SECRETS_MAX_ALLOWED)
        return -1;
    g_allowed[g_allowed_count++] = nc_xstrdup(value);
    return 0;
}

int secrets_scan(const char *text, int *pos, int *len, const char **labels, int max_results) {
    if (!text || !pos || !len)
        return 0;

    int found = 0;
    size_t text_len = strlen(text);

    for (size_t i = 0; i < text_len && found < max_results; i++) {
        for (int p = 0; patterns[p].label; p++) {
            if (i + (size_t)patterns[p].prefix_len > text_len)
                continue;
            if (memcmp(text + i, patterns[p].prefix, (size_t)patterns[p].prefix_len))
                continue;

            int kl = key_length(text + i + patterns[p].prefix_len);
            int total = patterns[p].prefix_len + kl;
            if (kl < patterns[p].min_key_len)
                continue;
            if (is_allowed(text + i, total))
                continue;

            pos[found] = (int)i;
            len[found] = total;
            if (labels)
                labels[found] = patterns[p].label;
            found++;
            i += (size_t)total - 1;
            break;
        }
    }
    return found;
}

bool secrets_contains(const char *text) {
    if (!text || !*text)
        return false;
    int pos[1], len[1];
    return secrets_scan(text, pos, len, NULL, 1) > 0;
}

/* Mask a single token: short tokens → "***", long → prefix...suffix */
static void mask_token(strbuf_t *out, const char *tok, int tok_len) {
    if (tok_len < 18) {
        strbuf_append_str(out, "***");
    } else {
        strbuf_append(out, tok, 6);
        strbuf_append_str(out, "...");
        strbuf_append(out, tok + tok_len - 4, 4);
    }
}

/* Scan for and redact DB connection string passwords: proto://user:PASS@host */
static void redact_db_connstrings(strbuf_t *out, const char *text, size_t text_len) {
    size_t last = 0;
    for (size_t i = 0; i < text_len;) {
        bool found = false;
        for (int d = 0; DB_PROTOS[d]; d++) {
            size_t plen = strlen(DB_PROTOS[d]);
            if (i + plen > text_len)
                continue;
            if (strncasecmp(text + i, DB_PROTOS[d], plen) != 0)
                continue;
            /* found DB proto; look for user:pass@host */
            const char *after = text + i + plen;
            size_t remain = text_len - i - plen;
            const char *at = memchr(after, '@', remain);
            if (!at)
                break;
            const char *colon = memchr(after, ':', (size_t)(at - after));
            if (!colon)
                break;
            /* emit everything up to the colon+1, mask password, emit @onwards */
            strbuf_append(out, text + last, (size_t)(colon + 1 - (text + last)));
            strbuf_append_str(out, "***");
            last = (size_t)(at - text);
            i = last;
            found = true;
            break;
        }
        if (!found)
            i++;
    }
    if (last > 0 && last < text_len)
        strbuf_append(out, text + last, text_len - last);
}

/* Scan for Authorization: Bearer <token> headers and mask the token */
static char *redact_auth_headers(const char *text) {
    const char *needle = "Authorization:";
    size_t nlen = strlen(needle);
    strbuf_t out;
    strbuf_init(&out);
    size_t tlen = strlen(text);
    size_t last = 0;

    for (size_t i = 0; i + nlen <= tlen; i++) {
        if (strncasecmp(text + i, needle, nlen) != 0)
            continue;
        size_t j = i + nlen;
        while (j < tlen && (text[j] == ' ' || text[j] == '\t'))
            j++;
        if (j + 7 > tlen || strncasecmp(text + j, "Bearer ", 7) != 0)
            continue;
        j += 7;
        while (j < tlen && text[j] == ' ')
            j++;
        size_t tok_start = j;
        while (j < tlen && !isspace((unsigned char)text[j]))
            j++;
        int tok_len = (int)(j - tok_start);
        if (tok_len < 10)
            continue;
        strbuf_append(&out, text + last, i - last);
        strbuf_append_str(&out, "Authorization: Bearer ");
        mask_token(&out, text + tok_start, tok_len);
        last = j;
    }
    if (last == 0) {
        strbuf_free(&out);
        return NULL;
    }
    strbuf_append(&out, text + last, tlen - last);
    return strbuf_detach(&out);
}

char *secrets_redact(const char *text) {
    if (!text || !*text || !g_redact_enabled)
        return nc_xstrdup(text ? text : "");

    size_t text_len = strlen(text);

    /* Phase 1: prefix-based token redaction */
    int positions[SECRETS_MAX_SCAN];
    int lengths[SECRETS_MAX_SCAN];
    int found = secrets_scan(text, positions, lengths, NULL, SECRETS_MAX_SCAN);

    strbuf_t out;
    strbuf_init(&out);

    if (found > 0) {
        int cursor = 0;
        for (int i = 0; i < found; i++) {
            if (positions[i] > cursor)
                strbuf_append(&out, text + cursor, (size_t)(positions[i] - cursor));
            mask_token(&out, text + positions[i], lengths[i]);
            cursor = positions[i] + lengths[i];
        }
        if ((size_t)cursor < text_len)
            strbuf_append(&out, text + cursor, text_len - (size_t)cursor);
    } else {
        strbuf_append(&out, text, text_len);
    }

    /* Phase 2: DB connection string passwords */
    {
        strbuf_t db_out;
        strbuf_init(&db_out);
        redact_db_connstrings(&db_out, out.data, out.len);
        if (db_out.len > 0) {
            strbuf_free(&out);
            out = db_out;
        } else {
            strbuf_free(&db_out);
        }
    }

    /* Phase 3: Authorization headers */
    {
        char *auth_result = redact_auth_headers(out.data);
        if (auth_result) {
            strbuf_free(&out);
            strbuf_init(&out);
            strbuf_append_str(&out, auth_result);
            free(auth_result);
        }
    }

    /* Phase 4: ENV assignment patterns (KEY=value where KEY looks secret) */
    {
        static const char *secret_names[] = {"API_KEY",     "APIKEY",     "TOKEN",    "SECRET",
                                             "PASSWORD",    "PASSWD",     "AUTH_KEY", "ACCESS_KEY",
                                             "PRIVATE_KEY", "CREDENTIAL", NULL};
        strbuf_t env_out;
        strbuf_init(&env_out);
        bool env_changed = false;
        const char *s = out.data;
        size_t slen = out.len;
        size_t last = 0;
        for (size_t i = 0; i + 3 < slen; i++) {
            if (s[i] != '=')
                continue;
            size_t name_start = i;
            while (name_start > last &&
                   (isalnum((unsigned char)s[name_start - 1]) || s[name_start - 1] == '_'))
                name_start--;
            size_t name_len = i - name_start;
            if (name_len < 4)
                continue;
            char name_upper[NC_TAG_MAX];
            if (name_len >= sizeof(name_upper))
                continue;
            for (size_t k = 0; k < name_len; k++)
                name_upper[k] = toupper((unsigned char)s[name_start + k]);
            name_upper[name_len] = '\0';
            bool match = false;
            for (int n = 0; secret_names[n]; n++) {
                if (strstr(name_upper, secret_names[n])) {
                    match = true;
                    break;
                }
            }
            if (!match)
                continue;
            size_t val_start = i + 1;
            if (val_start < slen && (s[val_start] == '"' || s[val_start] == '\''))
                val_start++;
            size_t val_end = val_start;
            while (val_end < slen && !isspace((unsigned char)s[val_end]) && s[val_end] != '"' &&
                   s[val_end] != '\'')
                val_end++;
            int val_len = (int)(val_end - val_start);
            if (val_len < 6)
                continue;
            strbuf_append(&env_out, s + last, i + 1 - last);
            mask_token(&env_out, s + val_start, val_len);
            last = val_end;
            env_changed = true;
        }
        if (env_changed) {
            if (last < slen)
                strbuf_append(&env_out, s + last, slen - last);
            strbuf_free(&out);
            out = env_out;
        } else {
            strbuf_free(&env_out);
        }
    }

    /* Phase 5: JSON field patterns ("api_key": "value") */
    {
        static const char *json_keys[] = {"api_key",       "apiKey",     "token",
                                          "secret",        "password",   "access_token",
                                          "refresh_token", "auth_token", NULL};
        strbuf_t json_out;
        strbuf_init(&json_out);
        bool json_changed = false;
        const char *s = out.data;
        size_t slen = out.len;
        size_t last = 0;
        for (size_t i = 0; i + 5 < slen; i++) {
            if (s[i] != '"')
                continue;
            for (int jk = 0; json_keys[jk]; jk++) {
                size_t klen = strlen(json_keys[jk]);
                if (i + 1 + klen + 1 >= slen)
                    continue;
                if (memcmp(s + i + 1, json_keys[jk], klen) != 0)
                    continue;
                if (s[i + 1 + klen] != '"')
                    continue;
                size_t after = i + 1 + klen + 1;
                while (after < slen && (s[after] == ':' || s[after] == ' ' || s[after] == '\t'))
                    after++;
                if (after >= slen || s[after] != '"')
                    continue;
                after++;
                size_t val_start = after;
                while (after < slen && s[after] != '"')
                    after++;
                int val_len = (int)(after - val_start);
                if (val_len < 6)
                    continue;
                strbuf_append(&json_out, s + last, val_start - last);
                mask_token(&json_out, s + val_start, val_len);
                last = after;
                json_changed = true;
                break;
            }
        }
        if (json_changed) {
            if (last < slen)
                strbuf_append(&json_out, s + last, slen - last);
            strbuf_free(&out);
            out = json_out;
        } else {
            strbuf_free(&json_out);
        }
    }

    return strbuf_detach(&out);
}

int secrets_count_patterns(void) {
    int n = 0;
    while (patterns[n].label)
        n++;
    return n;
}
