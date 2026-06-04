/*
 * feature_list.c — JSON-based feature list for structured incremental progress.
 */

#include "harness.h"

typedef struct {
    char description[FLIST_DESC_MAX];
    char category[FLIST_CATEGORY_MAX];
    bool passes;
    int attempts;
    long last_attempt;
    long passed_at;
} flist_entry_t;

static struct {
    flist_entry_t entries[FLIST_MAX_ENTRIES];
    int count;
    char path[NC_PATH_MAX];
    bool loaded;
    int total_marked;
} g_fl;

void feature_list_init(void) {
    memset(&g_fl, 0, sizeof(g_fl));
    nc_cwd_join(g_fl.path, sizeof(g_fl.path), FLIST_FILE);
}

void feature_list_free(void) {
    memset(&g_fl, 0, sizeof(g_fl));
}

int feature_list_load(void) {
    if (g_fl.loaded)
        return g_fl.count;
    g_fl.loaded = true;
    size_t sz = 0;
    char *raw = strbuf_read_file(g_fl.path, &sz);
    if (!raw || sz == 0) {
        free(raw);
        return 0;
    }
    cJSON *arr = cJSON_Parse(raw);
    free(raw);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return -1;
    }
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (g_fl.count >= FLIST_MAX_ENTRIES)
            break;
        flist_entry_t *e = &g_fl.entries[g_fl.count];
        memset(e, 0, sizeof(*e));
        cJSON *v;
        if ((v = cJSON_GetObjectItem(item, "description")) && cJSON_IsString(v))
            nc_strlcpy(e->description, v->valuestring, sizeof(e->description));
        if ((v = cJSON_GetObjectItem(item, "category")) && cJSON_IsString(v))
            nc_strlcpy(e->category, v->valuestring, sizeof(e->category));
        if ((v = cJSON_GetObjectItem(item, "passes")) && cJSON_IsBool(v))
            e->passes = cJSON_IsTrue(v);
        if ((v = cJSON_GetObjectItem(item, "attempts")) && cJSON_IsNumber(v))
            e->attempts = v->valueint;
        if ((v = cJSON_GetObjectItem(item, "last_attempt")) && cJSON_IsNumber(v))
            e->last_attempt = (long)v->valuedouble;
        if ((v = cJSON_GetObjectItem(item, "passed_at")) && cJSON_IsNumber(v))
            e->passed_at = (long)v->valuedouble;
        g_fl.count++;
    }
    cJSON_Delete(arr);
    return g_fl.count;
}

int feature_list_save(void) {
    cJSON *arr = cJSON_CreateArray();
    int i;
    for (i = 0; i < g_fl.count; i++) {
        flist_entry_t *e = &g_fl.entries[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "description", e->description);
        if (e->category[0])
            cJSON_AddStringToObject(obj, "category", e->category);
        cJSON_AddBoolToObject(obj, "passes", e->passes);
        if (e->attempts > 0)
            cJSON_AddNumberToObject(obj, "attempts", e->attempts);
        if (e->last_attempt > 0)
            cJSON_AddNumberToObject(obj, "last_attempt", e->last_attempt);
        if (e->passed_at > 0)
            cJSON_AddNumberToObject(obj, "passed_at", e->passed_at);
        cJSON_AddItemToArray(arr, obj);
    }
    char *json = nc_cjson_detach_pretty(arr);
    if (!json)
        return -1;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, json);
    strbuf_append_str(&sb, "\n");

    size_t body_len = sb.len;
    char *body = strbuf_detach(&sb);
    int rc = nc_write_file_atomic(g_fl.path, body, body_len);
    free(body);
    free(json);
    return rc;
}

int feature_list_add(const char *description, const char *category) {
    if (!description || g_fl.count >= FLIST_MAX_ENTRIES)
        return -1;
    flist_entry_t *e = &g_fl.entries[g_fl.count];
    memset(e, 0, sizeof(*e));
    nc_strlcpy(e->description, description, sizeof(e->description));
    if (category)
        nc_strlcpy(e->category, category, sizeof(e->category));
    e->passes = false;
    g_fl.count++;
    return g_fl.count - 1;
}

int feature_list_mark_pass(int idx) {
    if (idx < 0 || idx >= g_fl.count)
        return -1;
    if (g_fl.entries[idx].passes)
        return 0;
    g_fl.entries[idx].passes = true;
    g_fl.entries[idx].passed_at = nc_wall_sec();
    g_fl.total_marked++;
    return 0;
}

int feature_list_mark_fail(int idx) {
    if (idx < 0 || idx >= g_fl.count)
        return -1;
    g_fl.entries[idx].passes = false;
    g_fl.entries[idx].passed_at = 0;
    return 0;
}

int feature_list_record_attempt(int idx) {
    if (idx < 0 || idx >= g_fl.count)
        return -1;
    g_fl.entries[idx].attempts++;
    g_fl.entries[idx].last_attempt = nc_wall_sec();
    return 0;
}

int feature_list_count(void) {
    return g_fl.count;
}

int feature_list_passing_count(void) {
    int n = 0, i;
    for (i = 0; i < g_fl.count; i++)
        if (g_fl.entries[i].passes)
            n++;
    return n;
}

int feature_list_failing_count(void) {
    int n = 0, i;
    for (i = 0; i < g_fl.count; i++)
        if (!g_fl.entries[i].passes)
            n++;
    return n;
}

int feature_list_next_failing(void) {
    int best = -1, min_att = INT_MAX, i;
    for (i = 0; i < g_fl.count; i++) {
        if (!g_fl.entries[i].passes && g_fl.entries[i].attempts < min_att) {
            min_att = g_fl.entries[i].attempts;
            best = i;
        }
    }
    return best;
}

bool feature_list_all_passing(void) {
    int i;
    for (i = 0; i < g_fl.count; i++)
        if (!g_fl.entries[i].passes)
            return false;
    return g_fl.count > 0;
}

const char *feature_list_description(int idx) {
    if (idx < 0 || idx >= g_fl.count)
        return NULL;
    return g_fl.entries[idx].description;
}

bool feature_list_is_passing(int idx) {
    if (idx < 0 || idx >= g_fl.count)
        return false;
    return g_fl.entries[idx].passes;
}

char *feature_list_build_prompt(void) {
    if (g_fl.count == 0)
        return nc_xstrdup("");
    int passing = feature_list_passing_count();
    int failing = feature_list_failing_count();
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "<feature_list total=\"%d\" passing=\"%d\" failing=\"%d\">\n", g_fl.count,
                   passing, failing);
    int i;
    for (i = 0; i < g_fl.count; i++) {
        flist_entry_t *e = &g_fl.entries[i];
        strbuf_appendf(&sb, "  <feature idx=\"%d\" status=\"%s\"", i, e->passes ? "pass" : "fail");
        if (e->category[0])
            strbuf_appendf(&sb, " category=\"%s\"", e->category);
        if (e->attempts > 0)
            strbuf_appendf(&sb, " attempts=\"%d\"", e->attempts);
        strbuf_appendf(&sb, ">%s</feature>\n", e->description);
    }
    strbuf_append_str(&sb, "  <rule>NEVER remove, reword, or reorder feature entries. "
                           "Only change the \"passes\" field.</rule>\n");
    if (failing > 0) {
        int next = feature_list_next_failing();
        if (next >= 0)
            strbuf_appendf(&sb, "  <instruction>Work on feature %d next: %s</instruction>\n", next,
                           g_fl.entries[next].description);
    } else {
        strbuf_appendf(&sb,
                       "  <instruction>All features passing. Review for quality.</instruction>\n");
    }
    strbuf_appendf(&sb, "</feature_list>\n");
    return strbuf_detach(&sb);
}

char *feature_list_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    int passing = feature_list_passing_count();
    strbuf_appendf(&sb, "Feature List: %d/%d passing\n", passing, g_fl.count);
    int i;
    for (i = 0; i < g_fl.count; i++) {
        flist_entry_t *e = &g_fl.entries[i];
        strbuf_appendf(&sb, "  [%s] %s", e->passes ? "PASS" : "FAIL", e->description);
        if (e->category[0])
            strbuf_appendf(&sb, " (%s)", e->category);
        if (e->attempts > 0)
            strbuf_appendf(&sb, " [%d attempts]", e->attempts);
        strbuf_appendf(&sb, "\n");
    }
    return strbuf_detach(&sb);
}
