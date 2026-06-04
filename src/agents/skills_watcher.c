/*
 * skills_watcher.c — skill directory tracking
 */

#include "nevercode.h"

typedef struct {
    char path[NC_PATH_MAX];
    char name[NC_TAG_MAX];
    long mt;
    bool on;
} tr_t;

static struct {
    tr_t f[MAX_TRACKED_SKILLS];
    int n;
    char d[4][NC_PATH_MAX];
    int nd;
    long ls;
    int rl;
    bool ok;
} g;

static long mtime_of(const char *p) {
    struct stat s;
    if (stat(p, &s) != 0)
        return 0;
    return s.st_mtime;
}

static int find_path(const char *p) {
    for (int i = 0; i < g.n; i++)
        if (g.f[i].on && strcmp(g.f[i].path, p) == 0)
            return i;
    return -1;
}

static int track_file(const char *p, const char *nm) {
    int i = find_path(p);
    if (i >= 0) {
        g.f[i].mt = mtime_of(p);
        return i;
    }
    if (g.n >= MAX_TRACKED_SKILLS)
        return -1;
    tr_t *t = &g.f[g.n];
    nc_strlcpy(t->path, p, sizeof t->path);
    nc_strlcpy(t->name, nm, sizeof t->name);
    t->mt = mtime_of(p);
    t->on = true;
    return g.n++;
}

static int scan_dir(const char *dir) {
    DIR *D = opendir(dir);
    if (!D)
        return 0;
    int ad = 0;
    struct dirent *e;
    while ((e = readdir(D)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        size_t nl = strlen(e->d_name);
        int md = (nl > 3 && strcmp(e->d_name + nl - 3, ".md") == 0);
        int js = (nl > 5 && strcmp(e->d_name + nl - 5, ".json") == 0);
        if (!md && !js)
            continue;
        char p[NC_PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        char nm[NC_TAG_MAX];
        snprintf(nm, sizeof nm, "%.*s", (int)(nl - (md ? 3 : 5)), e->d_name);
        if (track_file(p, nm) >= 0)
            ad++;
    }
    closedir(D);
    return ad;
}

void skills_watcher_init(void) {
    memset(&g, 0, sizeof g);
    g.ok = true;
}

void skills_watcher_free(void) {
    memset(&g, 0, sizeof g);
}

int skills_watcher_add_dir(const char *dir) {
    if (!g.ok || g.nd >= 4)
        return -1;
    nc_strlcpy(g.d[g.nd], dir, NC_PATH_MAX);
    g.nd++;
    return scan_dir(dir);
}

bool skills_watcher_check(void) {
    if (!g.ok)
        return false;
    bool ch = false;
    for (int i = 0; i < g.n; i++) {
        if (!g.f[i].on)
            continue;
        long c = mtime_of(g.f[i].path);
        if (c == 0) {
            g.f[i].on = false;
            ch = true;
        } else if (c != g.f[i].mt) {
            g.f[i].mt = c;
            ch = true;
        }
    }
    for (int i = 0; i < g.nd; i++)
        if (scan_dir(g.d[i]) > 0)
            ch = true;
    g.ls = nc_wall_sec();
    if (ch)
        g.rl++;
    return ch;
}

int skills_watcher_count(void) {
    int c = 0;
    for (int i = 0; i < g.n; i++)
        if (g.f[i].on)
            c++;
    return c;
}

char *skills_watcher_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Skills Watcher: %d files tracked, %d reloads\n", skills_watcher_count(),
                   g.rl);
    for (int i = 0; i < g.n; i++)
        if (g.f[i].on)
            strbuf_appendf(&sb, "  %s -> %s\n", g.f[i].name, g.f[i].path);
    return strbuf_detach(&sb);
}
