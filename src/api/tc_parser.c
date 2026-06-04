/*
 * tc_parser.c — Extract structured tool calls from non-standard assistant text.
 *
 * Some OpenAI-compatible stacks stream tool invocations inside the message body
 * (XML-ish tags, delimiter blocks, or inline JSON) instead of a native tool_calls
 * array. This file registers small parsers that normalize those shapes for the
 * agent loop.
 */

#include "defaults.h"
#include "nevercode.h"

/* ── Registry ── */

typedef struct {
    char id[TC_PARSER_ID_MAX];
    tc_parse_fn fn;
} tc_parser_entry_t;

static tc_parser_entry_t g_parsers[TC_PARSER_MAX];
static int g_parser_n;

void tc_parser_init(void) {
    memset(g_parsers, 0, sizeof(g_parsers));
    g_parser_n = 0;
}

int tc_parser_register(const char *id, tc_parse_fn fn) {
    if (!id || !fn || g_parser_n >= TC_PARSER_MAX)
        return -1;
    nc_strlcpy(g_parsers[g_parser_n].id, id, TC_PARSER_ID_MAX);
    g_parsers[g_parser_n].fn = fn;
    g_parser_n++;
    return 0;
}

int tc_parser_count(void) {
    return g_parser_n;
}

const char *tc_parser_list(void) {
    static char buf[TC_PARSER_MAX * (TC_PARSER_ID_MAX + 2) + 1];
    size_t off = 0;
    for (int i = 0; i < g_parser_n; i++) {
        if (i > 0 && off + 2 < sizeof(buf)) {
            buf[off++] = ',';
            buf[off++] = ' ';
        }
        size_t id_len = strlen(g_parsers[i].id);
        if (off + id_len < sizeof(buf)) {
            memcpy(buf + off, g_parsers[i].id, id_len);
            off += id_len;
        }
    }
    buf[off] = '\0';
    return buf;
}

int tc_parser_parse(const char *parser_id, const char *text, tc_parse_result_t *out) {
    if (!parser_id || !text || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < g_parser_n; i++) {
        if (strcmp(g_parsers[i].id, parser_id) == 0)
            return g_parsers[i].fn(text, out);
    }
    return -1;
}

int tc_parser_auto(const char *text, tc_parse_result_t *out) {
    if (!text || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < g_parser_n; i++) {
        tc_parse_result_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        if (g_parsers[i].fn(text, &tmp) == 0 && tmp.n_calls > 0) {
            *out = tmp;
            return 0;
        }
    }
    return -1;
}

/* ── Utility: generate unique call id ── */

static void gen_call_id(char *buf, size_t sz) {
    static int counter = 0;
    snprintf(buf, sz, "call_%08x", ++counter);
}

/* ── Utility: extract JSON from text at offset, return end pos or -1 ── */

static int extract_json_object(const char *text, int start, char *out, int out_sz) {
    if (text[start] != '{')
        return -1;
    int depth = 0;
    int in_str = 0;
    int i = start;
    for (; text[i]; i++) {
        if (text[i] == '"' && (i == 0 || text[i - 1] != '\\'))
            in_str = !in_str;
        if (!in_str) {
            if (text[i] == '{')
                depth++;
            else if (text[i] == '}') {
                depth--;
                if (depth == 0) {
                    int len = i - start + 1;
                    if (len >= out_sz)
                        return -1;
                    memcpy(out, text + start, len);
                    out[len] = '\0';
                    return i;
                }
            }
        }
    }
    return -1;
}

/* ── Parser: <tool_call>{ ... JSON ... }</tool_call> ── */

static int parse_xml_tool_json(const char *text, tc_parse_result_t *out) {
    if (!strstr(text, "<tool_call>"))
        return -1;

    const char *first = strstr(text, "<tool_call>");
    if (first) {
        int plen = (int)(first - text);
        if (plen >= (int)sizeof(out->content))
            plen = sizeof(out->content) - 1;
        memcpy(out->content, text, plen);
        out->content[plen] = '\0';
    }

    const char *p = text;
    while ((p = strstr(p, "<tool_call>")) != NULL) {
        p += 11; /* skip <tool_call> */
        const char *end = strstr(p, "</tool_call>");
        int chunk_len;
        if (end)
            chunk_len = (int)(end - p);
        else
            chunk_len = (int)strlen(p);

        char buf[TC_PARSER_BUF_SIZE];
        if (chunk_len >= (int)sizeof(buf)) {
            p += chunk_len;
            continue;
        }
        memcpy(buf, p, chunk_len);
        buf[chunk_len] = '\0';

        /* Trim whitespace */
        char *s = buf;
        while (*s && isspace((unsigned char)*s))
            s++;
        int slen = (int)strlen(s);
        while (slen > 0 && isspace((unsigned char)s[slen - 1]))
            s[--slen] = '\0';

        cJSON *obj = cJSON_Parse(s);
        if (obj) {
            cJSON *name = cJSON_GetObjectItem(obj, "name");
            cJSON *args = cJSON_GetObjectItem(obj, "arguments");
            if (name && cJSON_IsString(name) && out->n_calls < TC_MAX_CALLS_PER_PARSE) {
                parsed_tool_call_t *tc = &out->calls[out->n_calls];
                nc_strlcpy(tc->name, name->valuestring, sizeof(tc->name));
                if (args) {
                    char *a = cJSON_PrintUnformatted(args);
                    if (a) {
                        nc_strlcpy(tc->arguments, a, sizeof(tc->arguments));
                        free(a);
                    }
                }
                gen_call_id(tc->id, sizeof(tc->id));
                out->n_calls++;
            }
            cJSON_Delete(obj);
        }

        if (end)
            p = end + 12;
        else
            break;
    }
    return out->n_calls > 0 ? 0 : -1;
}

/* ── Parser: unicode-delimited tool call blocks ── */

static const char DS_START[] = "\xef\xbd\x9c"
                               "tool\xe2\x96\x81"
                               "calls\xe2\x96\x81"
                               "begin\xef\xbd\x9c";
static const char DS_CALL_BEGIN[] = "\xef\xbd\x9c"
                                    "tool\xe2\x96\x81"
                                    "call\xe2\x96\x81"
                                    "begin\xef\xbd\x9c";
static const char DS_SEP[] = "\xef\xbd\x9c"
                             "tool\xe2\x96\x81"
                             "sep\xef\xbd\x9c";
static const char DS_CALL_END[] = "\xef\xbd\x9c"
                                  "tool\xe2\x96\x81"
                                  "call\xe2\x96\x81"
                                  "end\xef\xbd\x9c";

static int parse_deepseek(const char *text, tc_parse_result_t *out) {
    const char *start = strstr(text, DS_CALL_BEGIN);
    if (!start && !strstr(text, DS_START))
        return -1;
    if (!start)
        start = strstr(text, DS_START);
    if (!start)
        return -1;

    int plen = (int)(start - text);
    if (plen >= (int)sizeof(out->content))
        plen = sizeof(out->content) - 1;
    memcpy(out->content, text, plen);
    out->content[plen] = '\0';

    const char *p = text;
    while ((p = strstr(p, DS_CALL_BEGIN)) != NULL) {
        p += strlen(DS_CALL_BEGIN);
        const char *sep = strstr(p, DS_SEP);
        const char *end = strstr(p, DS_CALL_END);
        if (!sep || !end || sep >= end)
            break;

        int name_len = (int)(sep - p);
        char name_buf[NC_LABEL_MAX];
        if (name_len >= (int)sizeof(name_buf))
            name_len = sizeof(name_buf) - 1;
        memcpy(name_buf, p, name_len);
        name_buf[name_len] = '\0';

        const char *args_start = sep + strlen(DS_SEP);
        int args_len = (int)(end - args_start);
        char args_buf[TC_PARSER_BUF_SIZE];
        if (args_len >= (int)sizeof(args_buf))
            args_len = sizeof(args_buf) - 1;
        memcpy(args_buf, args_start, args_len);
        args_buf[args_len] = '\0';

        /* Trim */
        char *n = name_buf;
        while (*n && isspace((unsigned char)*n))
            n++;
        char *a = args_buf;
        while (*a && isspace((unsigned char)*a))
            a++;

        if (out->n_calls < TC_MAX_CALLS_PER_PARSE) {
            parsed_tool_call_t *tc = &out->calls[out->n_calls];
            nc_strlcpy(tc->name, n, sizeof(tc->name));
            nc_strlcpy(tc->arguments, a, sizeof(tc->arguments));
            gen_call_id(tc->id, sizeof(tc->id));
            out->n_calls++;
        }

        p = end + strlen(DS_CALL_END);
    }
    return out->n_calls > 0 ? 0 : -1;
}

/* ── Parser: XML tool_call with <function=name> and <parameter> nodes ── */

static int parse_qwen_coder(const char *text, tc_parse_result_t *out) {
    if (!strstr(text, "<tool_call>"))
        return -1;
    if (!strstr(text, "<function="))
        return -1;

    const char *first = strstr(text, "<tool_call>");
    int plen = (int)(first - text);
    if (plen >= (int)sizeof(out->content))
        plen = sizeof(out->content) - 1;
    memcpy(out->content, text, plen);
    out->content[plen] = '\0';

    const char *p = text;
    while ((p = strstr(p, "<tool_call>")) != NULL) {
        p += 11;
        const char *tc_end = strstr(p, "</tool_call>");
        int tc_len = tc_end ? (int)(tc_end - p) : (int)strlen(p);
        char tc_buf[TC_PARSER_BUF_SIZE];
        if (tc_len >= (int)sizeof(tc_buf)) {
            p += tc_len;
            continue;
        }
        memcpy(tc_buf, p, tc_len);
        tc_buf[tc_len] = '\0';

        char *fn_start = strstr(tc_buf, "<function=");
        if (!fn_start) {
            p += tc_len;
            continue;
        }
        fn_start += 10; /* skip <function= */
        char *fn_end = strchr(fn_start, '>');
        if (!fn_end) {
            p += tc_len;
            continue;
        }
        int name_len = (int)(fn_end - fn_start);
        char name[NC_LABEL_MAX];
        if (name_len >= (int)sizeof(name))
            name_len = sizeof(name) - 1;
        memcpy(name, fn_start, name_len);
        name[name_len] = '\0';

        /* Extract <parameter=key>value</parameter> pairs into JSON */
        cJSON *args = cJSON_CreateObject();
        char *pp = fn_end + 1;
        while ((pp = strstr(pp, "<parameter=")) != NULL) {
            pp += 11;
            char *key_end = strchr(pp, '>');
            if (!key_end)
                break;
            int klen = (int)(key_end - pp);
            char key[NC_TAG_MAX];
            if (klen >= (int)sizeof(key))
                klen = sizeof(key) - 1;
            memcpy(key, pp, klen);
            key[klen] = '\0';

            char *val_start = key_end + 1;
            char *val_end = strstr(val_start, "</parameter>");
            if (!val_end)
                break;
            int vlen = (int)(val_end - val_start);
            char val[TC_PARSER_VAL_SIZE];
            if (vlen >= (int)sizeof(val))
                vlen = sizeof(val) - 1;
            memcpy(val, val_start, vlen);
            val[vlen] = '\0';

            /* Try to parse as JSON literal */
            cJSON *jval = cJSON_Parse(val);
            if (jval)
                cJSON_AddItemToObject(args, key, jval);
            else
                cJSON_AddStringToObject(args, key, val);

            pp = val_end + 12;
        }

        if (out->n_calls < TC_MAX_CALLS_PER_PARSE) {
            parsed_tool_call_t *tc = &out->calls[out->n_calls];
            nc_strlcpy(tc->name, name, sizeof(tc->name));
            char *a = cJSON_PrintUnformatted(args);
            if (a) {
                nc_strlcpy(tc->arguments, a, sizeof(tc->arguments));
                free(a);
            }
            gen_call_id(tc->id, sizeof(tc->id));
            out->n_calls++;
        }
        cJSON_Delete(args);

        if (tc_end)
            p = tc_end + 12;
        else
            break;
    }
    return out->n_calls > 0 ? 0 : -1;
}

/* ── Parser: inline JSON objects with "name" + "arguments" / "parameters" ── */

static int parse_llama_json(const char *text, tc_parse_result_t *out) {
    const char *bot = "<|python_tag|>";
    const char *scan = strstr(text, bot);
    if (!scan && !strchr(text, '{'))
        return -1;

    if (scan) {
        int plen = (int)(scan - text);
        if (plen >= (int)sizeof(out->content))
            plen = sizeof(out->content) - 1;
        memcpy(out->content, text, plen);
        out->content[plen] = '\0';
    }

    const char *p = scan ? scan + strlen(bot) : text;
    while (*p) {
        while (*p && *p != '{')
            p++;
        if (!*p)
            break;

        char json_buf[TC_PARSER_BUF_SIZE];
        int end = extract_json_object(p, 0, json_buf, sizeof(json_buf));
        if (end < 0) {
            p++;
            continue;
        }

        cJSON *obj = cJSON_Parse(json_buf);
        if (obj) {
            cJSON *name = cJSON_GetObjectItem(obj, "name");
            cJSON *args = cJSON_GetObjectItem(obj, "arguments");
            if (!args)
                args = cJSON_GetObjectItem(obj, "parameters");

            if (name && cJSON_IsString(name) && args && out->n_calls < TC_MAX_CALLS_PER_PARSE) {
                parsed_tool_call_t *tc = &out->calls[out->n_calls];
                nc_strlcpy(tc->name, name->valuestring, sizeof(tc->name));
                char *a = cJSON_PrintUnformatted(args);
                if (a) {
                    nc_strlcpy(tc->arguments, a, sizeof(tc->arguments));
                    free(a);
                }
                gen_call_id(tc->id, sizeof(tc->id));
                out->n_calls++;

                if (!scan) {
                    int pre = (int)(p - text);
                    if (pre > 0 && out->content[0] == '\0') {
                        if (pre >= (int)sizeof(out->content))
                            pre = sizeof(out->content) - 1;
                        memcpy(out->content, text, pre);
                        out->content[pre] = '\0';
                    }
                }
            }
            cJSON_Delete(obj);
        }
        p += end + 1;
    }
    return out->n_calls > 0 ? 0 : -1;
}

/* ── Parser: redacted segment tags for tool id + argument blob ── */

static int parse_kimi_k2(const char *text, tc_parse_result_t *out) {
    const char *s1 = "<|tool_calls_section_begin|>";
    const char *s2 = "<|tool_call_section_begin|>";
    const char *start = strstr(text, s1);
    if (!start)
        start = strstr(text, s2);
    if (!start)
        return -1;

    int plen = (int)(start - text);
    if (plen >= (int)sizeof(out->content))
        plen = sizeof(out->content) - 1;
    memcpy(out->content, text, plen);
    out->content[plen] = '\0';

    const char *p = start;
    const char *cb = "<|tool_call_begin|>";
    const char *ab = "<|tool_call_argument_begin|>";
    const char *ce = "<|tool_call_end|>";

    while ((p = strstr(p, cb)) != NULL) {
        p += strlen(cb);
        while (*p && isspace((unsigned char)*p))
            p++;

        const char *arg_begin = strstr(p, ab);
        if (!arg_begin)
            break;
        int id_len = (int)(arg_begin - p);
        char call_id[NC_TAG_MAX];
        if (id_len >= (int)sizeof(call_id))
            id_len = sizeof(call_id) - 1;
        memcpy(call_id, p, id_len);
        call_id[id_len] = '\0';
        /* Trim */
        int l = (int)strlen(call_id);
        while (l > 0 && isspace((unsigned char)call_id[l - 1]))
            call_id[--l] = '\0';

        const char *args_start = arg_begin + strlen(ab);
        const char *call_end = strstr(args_start, ce);
        int args_len = call_end ? (int)(call_end - args_start) : (int)strlen(args_start);
        char args_buf[TC_PARSER_BUF_SIZE];
        if (args_len >= (int)sizeof(args_buf))
            args_len = sizeof(args_buf) - 1;
        memcpy(args_buf, args_start, args_len);
        args_buf[args_len] = '\0';

        /* Extract function name: "functions.get_weather:0" -> "get_weather" */
        char func_name[NC_TAG_MAX];
        char *colon = strchr(call_id, ':');
        char *id_part = call_id;
        if (colon) {
            *colon = '\0';
        }
        char *dot = strrchr(id_part, '.');
        if (dot)
            id_part = dot + 1;
        nc_strlcpy(func_name, id_part, sizeof(func_name));

        if (out->n_calls < TC_MAX_CALLS_PER_PARSE) {
            parsed_tool_call_t *tc = &out->calls[out->n_calls];
            nc_strlcpy(tc->name, func_name, sizeof(tc->name));
            /* Trim args whitespace */
            char *a = args_buf;
            while (*a && isspace((unsigned char)*a))
                a++;
            nc_strlcpy(tc->arguments, a, sizeof(tc->arguments));
            nc_strlcpy(tc->id, call_id, sizeof(tc->id));
            out->n_calls++;
        }

        p = call_end ? call_end + strlen(ce) : args_start + args_len;
    }
    return out->n_calls > 0 ? 0 : -1;
}

/* ── Parser: [TOOL_CALLS] prefix (JSON array/object or inline name{args}) ── */

static int parse_mistral(const char *text, tc_parse_result_t *out) {
    const char *token = "[TOOL_CALLS]";
    if (!strstr(text, token))
        return -1;

    const char *first = strstr(text, token);
    int plen = (int)(first - text);
    if (plen >= (int)sizeof(out->content))
        plen = sizeof(out->content) - 1;
    memcpy(out->content, text, plen);
    out->content[plen] = '\0';

    const char *after = first + strlen(token);
    /* Trim */
    while (*after && isspace((unsigned char)*after))
        after++;

    if (*after == '[' || *after == '{') {
        /* Pre-v11: JSON array or object */
        cJSON *arr = cJSON_Parse(after);
        if (!arr && *after == '{') {
            /* Single object */
            char wrap[TC_PARSER_BUF_SIZE + 128];
            snprintf(wrap, sizeof(wrap), "[%s]", after);
            arr = cJSON_Parse(wrap);
        }
        if (arr && cJSON_IsArray(arr)) {
            int n = cJSON_GetArraySize(arr);
            for (int i = 0; i < n && out->n_calls < TC_MAX_CALLS_PER_PARSE; i++) {
                cJSON *item = cJSON_GetArrayItem(arr, i);
                cJSON *name = cJSON_GetObjectItem(item, "name");
                cJSON *args = cJSON_GetObjectItem(item, "arguments");
                if (name && cJSON_IsString(name)) {
                    parsed_tool_call_t *tc = &out->calls[out->n_calls];
                    nc_strlcpy(tc->name, name->valuestring, sizeof(tc->name));
                    if (args) {
                        char *a = cJSON_PrintUnformatted(args);
                        if (a) {
                            nc_strlcpy(tc->arguments, a, sizeof(tc->arguments));
                            free(a);
                        }
                    }
                    gen_call_id(tc->id, sizeof(tc->id));
                    out->n_calls++;
                }
            }
            cJSON_Delete(arr);
        }
    } else {
        /* v11+: [TOOL_CALLS]tool_name{args}[TOOL_CALLS]... */
        const char *p = first;
        while ((p = strstr(p, token)) != NULL) {
            p += strlen(token);
            while (*p && isspace((unsigned char)*p))
                p++;
            const char *brace = strchr(p, '{');
            if (!brace)
                break;
            int name_len = (int)(brace - p);
            char name[NC_LABEL_MAX];
            if (name_len >= (int)sizeof(name))
                name_len = sizeof(name) - 1;
            memcpy(name, p, name_len);
            name[name_len] = '\0';
            /* Trim name */
            int nl = (int)strlen(name);
            while (nl > 0 && isspace((unsigned char)name[nl - 1]))
                name[--nl] = '\0';

            char json_buf[TC_PARSER_BUF_SIZE];
            int end = extract_json_object(brace, 0, json_buf, sizeof(json_buf));
            if (end < 0) {
                p = brace + 1;
                continue;
            }

            if (out->n_calls < TC_MAX_CALLS_PER_PARSE) {
                parsed_tool_call_t *tc = &out->calls[out->n_calls];
                nc_strlcpy(tc->name, name, sizeof(tc->name));
                nc_strlcpy(tc->arguments, json_buf, sizeof(tc->arguments));
                gen_call_id(tc->id, sizeof(tc->id));
                out->n_calls++;
            }
            p = brace + end + 1;
        }
    }
    return out->n_calls > 0 ? 0 : -1;
}

/* ── Parser: <tool_call>func\n<arg_key>k</arg_key><arg_value>v</arg_value>\n</tool_call> (GLM) ──
 */

static int parse_glm(const char *text, tc_parse_result_t *out) {
    if (!strstr(text, "<tool_call>"))
        return -1;

    const char *first = strstr(text, "<tool_call>");
    if (first) {
        int plen = (int)(first - text);
        if (plen >= (int)sizeof(out->content))
            plen = sizeof(out->content) - 1;
        memcpy(out->content, text, plen);
        out->content[plen] = '\0';
    }

    const char *p = text;
    while ((p = strstr(p, "<tool_call>")) != NULL) {
        p += 11;
        const char *end = strstr(p, "</tool_call>");
        if (!end)
            break;

        char func_name[TC_PARSER_VAL_SIZE] = {0};
        const char *nl = memchr(p, '\n', end - p);
        if (!nl)
            nl = end;
        int nlen = (int)(nl - p);
        if (nlen <= 0 || nlen >= (int)sizeof(func_name)) {
            p = end + 12;
            continue;
        }
        memcpy(func_name, p, nlen);
        func_name[nlen] = '\0';
        for (int i = nlen - 1; i >= 0 && (func_name[i] == ' ' || func_name[i] == '\r'); i--)
            func_name[i] = '\0';

        if (strchr(func_name, '{')) {
            p = end + 12;
            continue;
        }

        cJSON *args = cJSON_CreateObject();
        const char *kp = nl;
        while (kp < end) {
            const char *ks = strstr(kp, "<arg_key>");
            if (!ks || ks >= end)
                break;
            ks += 9;
            const char *ke = strstr(ks, "</arg_key>");
            if (!ke || ke >= end)
                break;

            char key[TC_PARSER_VAL_SIZE] = {0};
            int kl = (int)(ke - ks);
            if (kl > 0 && kl < (int)sizeof(key)) {
                memcpy(key, ks, kl);
                key[kl] = '\0';
            }

            const char *vs = strstr(ke, "<arg_value>");
            if (!vs || vs >= end) {
                kp = ke + 10;
                continue;
            }
            vs += 11;
            const char *ve = strstr(vs, "</arg_value>");
            if (!ve || ve >= end) {
                kp = vs;
                continue;
            }

            char val[TC_PARSER_VAL_SIZE] = {0};
            int vl = (int)(ve - vs);
            if (vl > 0 && vl < (int)sizeof(val)) {
                memcpy(val, vs, vl);
                val[vl] = '\0';
            }

            cJSON *jv = cJSON_Parse(val);
            if (jv)
                cJSON_AddItemToObject(args, key, jv);
            else
                cJSON_AddStringToObject(args, key, val);

            kp = ve + 12;
        }

        char *json_str = nc_cjson_detach(args);
        if (json_str && out->n_calls < TC_MAX_CALLS_PER_PARSE) {
            parsed_tool_call_t *tc = &out->calls[out->n_calls];
            nc_strlcpy(tc->name, func_name, sizeof(tc->name));
            nc_strlcpy(tc->arguments, json_str, sizeof(tc->arguments));
            gen_call_id(tc->id, sizeof(tc->id));
            out->n_calls++;
        }
        free(json_str);
        p = end + 12;
    }
    return out->n_calls > 0 ? 0 : -1;
}

/* ── Parser: <longcat_tool_call>{ JSON }</longcat_tool_call> ── */

static int parse_longcat(const char *text, tc_parse_result_t *out) {
    static const char *open_tag = "<longcat_tool_call>";
    static const char *close_tag = "</longcat_tool_call>";
    if (!strstr(text, open_tag))
        return -1;

    const char *first = strstr(text, open_tag);
    if (first) {
        int plen = (int)(first - text);
        if (plen >= (int)sizeof(out->content))
            plen = sizeof(out->content) - 1;
        memcpy(out->content, text, plen);
        out->content[plen] = '\0';
    }

    const char *p = text;
    while ((p = strstr(p, open_tag)) != NULL) {
        p += 19;
        while (*p && isspace((unsigned char)*p))
            p++;
        char json_buf[TC_PARSER_BUF_SIZE] = {0};
        int end = extract_json_object(p, 0, json_buf, sizeof(json_buf));
        if (end < 0) {
            p++;
            continue;
        }
        cJSON *root = cJSON_Parse(json_buf);
        if (!root) {
            p += end + 1;
            continue;
        }
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
        cJSON *args = cJSON_GetObjectItem(root, "arguments");
        if (name && out->n_calls < TC_MAX_CALLS_PER_PARSE) {
            parsed_tool_call_t *tc = &out->calls[out->n_calls];
            nc_strlcpy(tc->name, name, sizeof(tc->name));
            if (args) {
                char *as = cJSON_PrintUnformatted(args);
                if (as) {
                    nc_strlcpy(tc->arguments, as, sizeof(tc->arguments));
                    free(as);
                }
            } else {
                nc_strlcpy(tc->arguments, "{}", sizeof(tc->arguments));
            }
            gen_call_id(tc->id, sizeof(tc->id));
            out->n_calls++;
        }
        cJSON_Delete(root);
        const char *ct = strstr(p, close_tag);
        p = ct ? ct + 20 : p + end + 1;
    }
    return out->n_calls > 0 ? 0 : -1;
}

/* ── Auto-registration of built-in parsers ── */

__attribute__((constructor)) static void tc_parser_register_builtins(void) {
    tc_parser_init();
    tc_parser_register(NC_TC_PARSER_XML_JSON_TOOL, parse_xml_tool_json);
    tc_parser_register("qwen", parse_xml_tool_json);
    tc_parser_register("qwen25", parse_xml_tool_json);
    tc_parser_register("deepseek_v3", parse_deepseek);
    tc_parser_register("deepseek_v31", parse_deepseek);
    tc_parser_register("qwen3_coder", parse_qwen_coder);
    tc_parser_register("llama_json", parse_llama_json);
    tc_parser_register("llama3_json", parse_llama_json);
    tc_parser_register("llama4_json", parse_llama_json);
    tc_parser_register("kimi_k2", parse_kimi_k2);
    tc_parser_register("mistral", parse_mistral);
    tc_parser_register("glm45", parse_glm);
    tc_parser_register("glm47", parse_glm);
    tc_parser_register("longcat", parse_longcat);
}
