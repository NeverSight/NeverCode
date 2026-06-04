/*
 * node_artifact.c — Per-node structured artifact storage.
 *
 * Each pipeline node gets a directory under the run root where named
 * artifacts (stdout, stderr, trace, output, result metadata) are
 * stored as individual files.  Supports both write-once and append
 * semantics for streaming output capture.
 */

#include "harness.h"

void artifact_store_init(artifact_store_t *as, const char *root_dir) {
    memset(as, 0, sizeof(*as));
    if (root_dir)
        nc_strlcpy(as->root_dir, root_dir, sizeof(as->root_dir));
}

void artifact_store_free(artifact_store_t *as) {
    memset(as, 0, sizeof(*as));
}

static int _register_slot_from_disk(node_artifact_t *na, const char *name, const char *path,
                                    long size) {
    if (!na || !name || !path || na->nslots >= NODE_ART_MAX_SLOTS)
        return -1;

    node_art_slot_t *slot = &na->slots[na->nslots++];
    memset(slot, 0, sizeof(*slot));
    nc_strlcpy(slot->name, name, sizeof(slot->name));
    nc_strlcpy(slot->path, path, sizeof(slot->path));
    slot->size = size;
    return 0;
}

int artifact_store_rebuild(artifact_store_t *as) {
    if (!as)
        return -1;

    memset(as->nodes, 0, sizeof(as->nodes));
    as->nnodes = 0;

    if (!as->root_dir[0])
        return 0;

    struct stat root_st;
    if (stat(as->root_dir, &root_st) != 0 || !S_ISDIR(root_st.st_mode))
        return 0;

    DIR *root = opendir(as->root_dir);
    if (!root)
        return -1;

    struct dirent *node_ent;
    while ((node_ent = readdir(root)) != NULL && as->nnodes < PIPE_MAX_NODES) {
        if (node_ent->d_name[0] == '.')
            continue;

        char node_dir[NODE_ART_PATH_MAX];
        snprintf(node_dir, sizeof(node_dir), "%s/%s", as->root_dir, node_ent->d_name);

        struct stat node_st;
        if (stat(node_dir, &node_st) != 0 || !S_ISDIR(node_st.st_mode))
            continue;

        node_artifact_t *na = &as->nodes[as->nnodes++];
        memset(na, 0, sizeof(*na));
        nc_strlcpy(na->node_id, node_ent->d_name, sizeof(na->node_id));
        nc_strlcpy(na->base_dir, node_dir, sizeof(na->base_dir));

        DIR *node_dp = opendir(node_dir);
        if (!node_dp)
            continue;

        struct dirent *slot_ent;
        while ((slot_ent = readdir(node_dp)) != NULL && na->nslots < NODE_ART_MAX_SLOTS) {
            if (slot_ent->d_name[0] == '.')
                continue;

            char slot_path[NODE_ART_PATH_MAX];
            snprintf(slot_path, sizeof(slot_path), "%s/%s", node_dir, slot_ent->d_name);

            struct stat slot_st;
            if (stat(slot_path, &slot_st) != 0 || !S_ISREG(slot_st.st_mode))
                continue;

            (void)_register_slot_from_disk(na, slot_ent->d_name, slot_path, (long)slot_st.st_size);
        }
        closedir(node_dp);
    }

    closedir(root);
    return 0;
}

static node_artifact_t *_find_node(const artifact_store_t *as, const char *node_id) {
    for (int i = 0; i < as->nnodes; i++)
        if (strcmp(as->nodes[i].node_id, node_id) == 0)
            return (node_artifact_t *)&as->nodes[i];
    return NULL;
}

int artifact_store_ensure_node(artifact_store_t *as, const char *node_id) {
    if (!node_id)
        return -1;
    if (_find_node(as, node_id))
        return 0;
    if (as->nnodes >= PIPE_MAX_NODES)
        return -1;

    node_artifact_t *na = &as->nodes[as->nnodes++];
    memset(na, 0, sizeof(*na));
    nc_strlcpy(na->node_id, node_id, sizeof(na->node_id));
    snprintf(na->base_dir, sizeof(na->base_dir), "%s/%s", as->root_dir, node_id);
    nc_mkdir_p(na->base_dir);
    return 0;
}

static node_art_slot_t *_find_slot(node_artifact_t *na, const char *name) {
    for (int i = 0; i < na->nslots; i++)
        if (strcmp(na->slots[i].name, name) == 0)
            return &na->slots[i];
    return NULL;
}

static node_art_slot_t *_ensure_slot(node_artifact_t *na, const char *name) {
    node_art_slot_t *s = _find_slot(na, name);
    if (s)
        return s;
    if (na->nslots >= NODE_ART_MAX_SLOTS)
        return NULL;

    s = &na->slots[na->nslots++];
    nc_strlcpy(s->name, name, sizeof(s->name));
    snprintf(s->path, sizeof(s->path), "%s/%s", na->base_dir, name);
    s->size = 0;
    return s;
}

int artifact_append(artifact_store_t *as, const char *node_id, const char *name,
                    const char *content) {
    if (!node_id || !name || !content)
        return -1;
    artifact_store_ensure_node(as, node_id);

    node_artifact_t *na = _find_node(as, node_id);
    if (!na)
        return -1;

    node_art_slot_t *slot = _ensure_slot(na, name);
    if (!slot)
        return -1;

    FILE *f = fopen(slot->path, "a");
    if (!f)
        return -1;
    size_t clen = strlen(content);
    size_t written = fwrite(content, 1, clen, f);
    int fe = fflush(f);
    fclose(f);
    if (written < clen || fe)
        return -1;

    slot->size += (long)written;
    return (int)written;
}

int artifact_write(artifact_store_t *as, const char *node_id, const char *name,
                   const char *content) {
    if (!node_id || !name)
        return -1;
    artifact_store_ensure_node(as, node_id);

    node_artifact_t *na = _find_node(as, node_id);
    if (!na)
        return -1;

    node_art_slot_t *slot = _ensure_slot(na, name);
    if (!slot)
        return -1;

    FILE *f = fopen(slot->path, "w");
    if (!f)
        return -1;
    size_t len = content ? strlen(content) : 0;
    int rc = 0;
    if (len > 0 && fwrite(content, 1, len, f) < len)
        rc = -1;
    if (rc == 0 && fflush(f) != 0)
        rc = -1;
    fclose(f);
    if (rc)
        return -1;

    slot->size = (long)len;
    return (int)len;
}

char *artifact_read(const artifact_store_t *as, const char *node_id, const char *name) {
    node_artifact_t *na = _find_node(as, node_id);
    if (!na)
        return NULL;

    node_art_slot_t *slot = _find_slot(na, name);
    if (!slot)
        return NULL;

    FILE *f = fopen(slot->path, "r");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > (long)NC_READ_CAP_HARNESS) {
        fclose(f);
        return sz == 0 ? nc_xstrdup("") : NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

const char *artifact_path(const artifact_store_t *as, const char *node_id, const char *name) {
    node_artifact_t *na = _find_node(as, node_id);
    if (!na)
        return NULL;

    node_art_slot_t *slot = _find_slot(na, name);
    return slot ? slot->path : NULL;
}

int artifact_slot_count(const artifact_store_t *as, const char *node_id) {
    node_artifact_t *na = _find_node(as, node_id);
    return na ? na->nslots : 0;
}

static void _attempt_name(char *buf, size_t sz, const char *name, int attempt) {
    const char *dot = strrchr(name, '.');
    if (dot && dot != name) {
        size_t stem = (size_t)(dot - name);
        snprintf(buf, sz, "%.*s-attempt-%d%s", (int)stem, name, attempt, dot);
    } else {
        snprintf(buf, sz, "%s-attempt-%d", name, attempt);
    }
}

int artifact_write_attempt(artifact_store_t *as, const char *node_id, const char *name, int attempt,
                           const char *content) {
    if (!name || attempt < 1)
        return -1;
    char aname[NODE_ART_NAME_MAX];
    _attempt_name(aname, sizeof(aname), name, attempt);
    return artifact_write(as, node_id, aname, content);
}

int artifact_append_attempt(artifact_store_t *as, const char *node_id, const char *name,
                            int attempt, const char *content) {
    if (!name || attempt < 1)
        return -1;
    char aname[NODE_ART_NAME_MAX];
    _attempt_name(aname, sizeof(aname), name, attempt);
    return artifact_append(as, node_id, aname, content);
}

char *artifact_format_manifest(const artifact_store_t *as, const char *node_id) {
    node_artifact_t *na = _find_node(as, node_id);
    if (!na)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Artifacts for '%s': %d files\n", na->node_id, na->nslots);

    for (int i = 0; i < na->nslots; i++) {
        const node_art_slot_t *s = &na->slots[i];
        strbuf_appendf(&sb, "  %s (%ld bytes)\n", s->name, s->size);
    }
    return strbuf_detach(&sb);
}
