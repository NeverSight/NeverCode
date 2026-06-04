#include "nevercode.h"
#include "defaults.h"

static int cmp_mtime_desc(const void *a, const void *b) {
    const memdir_header_t *ha = a;
    const memdir_header_t *hb = b;
    if (hb->mtime > ha->mtime)
        return 1;
    if (hb->mtime < ha->mtime)
        return -1;
    return 0;
}

static char *parse_frontmatter_field(const char *content, const char *field) {
    if (!content || !field)
        return NULL;
    char needle[NC_TAG_MAX];
    snprintf(needle, sizeof(needle), "%s:", field);
    const char *p = content;
    if (!nc_starts_with(p, "---"))
        return NULL;
    p += 3;
    while (*p == '\n' || *p == '\r')
        p++;
    const char *end = strstr(p, "\n---");
    if (!end)
        return NULL;
    while (p < end) {
        const char *eol = strchr(p, '\n');
        if (!eol || eol > end)
            eol = end;
        size_t line_len = (size_t)(eol - p);
        size_t nlen = strlen(needle);
        if (line_len > nlen && strncmp(p, needle, nlen) == 0) {
            const char *val = p + nlen;
            while (*val == ' ' || *val == '\t')
                val++;
            size_t vlen = (size_t)(eol - val);
            while (vlen > 0 && (val[vlen - 1] == '\r' || val[vlen - 1] == ' '))
                vlen--;
            if (vlen > 0) {
                char *r = malloc(vlen + 1);
                if (!r)
                    return NULL;
                memcpy(r, val, vlen);
                r[vlen] = '\0';
                return r;
            }
        }
        p = eol + 1;
    }
    return NULL;
}

void memdir_init(void) {}
void memdir_free(void) {}

int memdir_scan(const char *dir, memdir_header_t *out, int max) {
    if (!dir || !out || max <= 0)
        return 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max) {
        size_t nlen = strlen(ent->d_name);
        if (nlen < 4 || strcmp(ent->d_name + nlen - 3, ".md") != 0)
            continue;
        if (strcmp(ent->d_name, MEMDIR_ENTRYPOINT) == 0)
            continue;
        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        char preview[MEMDIR_FRONTMATTER_LINES * 128];
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        size_t rd = fread(preview, 1, sizeof(preview) - 1, f);
        fclose(f);
        preview[rd] = '\0';
        memdir_header_t *h = &out[count];
        memset(h, 0, sizeof(*h));
        h->filename = nc_xstrdup(ent->d_name);
        h->filepath = nc_xstrdup(path);
        h->mtime = (long)st.st_mtime;
        h->description = parse_frontmatter_field(preview, "description");
        char *type_str = parse_frontmatter_field(preview, "type");
        if (type_str) {
            if (strcmp(type_str, "fact") == 0)
                h->type = 1;
            else if (strcmp(type_str, "procedure") == 0)
                h->type = 2;
            else if (strcmp(type_str, "preference") == 0)
                h->type = 3;
            else if (strcmp(type_str, "decision") == 0)
                h->type = 4;
            else if (strcmp(type_str, "context") == 0)
                h->type = 5;
            free(type_str);
        }
        count++;
    }
    closedir(d);
    qsort(out, (size_t)count, sizeof(memdir_header_t), cmp_mtime_desc);
    if (count > MEMDIR_MAX_FILES)
        count = MEMDIR_MAX_FILES;
    return count;
}

char *memdir_format_listing(const memdir_header_t *hdrs, int count) {
    if (!hdrs || count <= 0)
        return nc_xstrdup("(no memories)");
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < count; i++) {
        strbuf_append_str(&sb, "- ");
        strbuf_append_str(&sb, hdrs[i].filename);
        if (hdrs[i].description) {
            strbuf_append_str(&sb, " -- ");
            strbuf_append_str(&sb, hdrs[i].description);
        }
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}

int memdir_read_entrypoint(const char *dir, char **content_out) {
    if (!dir || !content_out)
        return -1;
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, MEMDIR_ENTRYPOINT);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        *content_out = nc_xstrdup("");
        return 0;
    }
    if ((size_t)sz > MEMDIR_ENTRYPOINT_MAX_BYTES)
        sz = MEMDIR_ENTRYPOINT_MAX_BYTES;
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    int lines = 0;
    for (size_t i = 0; i < rd; i++)
        if (buf[i] == '\n')
            lines++;
    if (lines > MEMDIR_ENTRYPOINT_MAX_LINES) {
        int lc = 0;
        for (size_t i = 0; i < rd; i++)
            if (buf[i] == '\n' && ++lc >= MEMDIR_ENTRYPOINT_MAX_LINES) {
                buf[i + 1] = '\0';
                break;
            }
    }
    *content_out = buf;
    return 0;
}

int memdir_write_entrypoint(const char *dir, const char *content) {
    if (!dir || !content)
        return -1;
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, MEMDIR_ENTRYPOINT);
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    int rc = (fputs(content, f) == EOF || fflush(f)) ? -1 : 0;
    fclose(f);
    return rc;
}

bool memdir_entrypoint_exists(const char *dir) {
    if (!dir)
        return false;
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, MEMDIR_ENTRYPOINT);
    return nc_file_exists(path);
}

void memdir_free_headers(memdir_header_t *hdrs, int count) {
    if (!hdrs)
        return;
    for (int i = 0; i < count; i++) {
        free(hdrs[i].filename);
        free(hdrs[i].filepath);
        free(hdrs[i].description);
    }
}
