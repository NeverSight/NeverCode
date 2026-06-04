#include "conversation.h"

void conversation_init(conversation_t *c) {
    memset(c, 0, sizeof(*c));
}

void conversation_free(conversation_t *c) {
    for (int i = 0; i < c->len; i++)
        message_free(&c->msgs[i]);
    free(c->msgs);
    free(c->system_prompt);
    memset(c, 0, sizeof(*c));
}

void conversation_clear_messages(conversation_t *c) {
    if (!c)
        return;
    for (int i = 0; i < c->len; i++)
        message_free(&c->msgs[i]);
    free(c->msgs);
    c->msgs = NULL;
    c->len = 0;
    c->cap = 0;
}

message_t *conversation_add(conversation_t *c, message_role_t role) {
    if (c->len >= c->cap) {
        int newcap = c->cap ? c->cap * CONV_GROW_FACTOR : CONV_INIT_CAP;
        message_t *tmp = realloc(c->msgs, (size_t)newcap * sizeof(message_t));
        if (!tmp)
            nc_fatal("out of memory (conversation_add)");
        c->msgs = tmp;
        c->cap = newcap;
    }
    message_t *m = &c->msgs[c->len++];
    message_init(m, role);
    return m;
}

void conversation_add_user_text(conversation_t *c, const char *text) {
    message_t *m = conversation_add(c, ROLE_USER);
    message_add_text(m, text);
}

message_t *conversation_add_user_multimodal(conversation_t *c) {
    return conversation_add(c, ROLE_USER);
}

void conversation_remove_at(conversation_t *c, int idx) {
    if (!c || idx < 0 || idx >= c->len)
        return;
    message_free(&c->msgs[idx]);
    int tail = c->len - idx - 1;
    if (tail > 0)
        memmove(&c->msgs[idx], &c->msgs[idx + 1], (size_t)tail * sizeof(message_t));
    c->len--;
}

message_t *conversation_insert_at(conversation_t *c, int idx, message_role_t role) {
    if (!c || idx < 0)
        return NULL;
    if (idx > c->len)
        idx = c->len;
    if (c->len >= c->cap) {
        int newcap = c->cap ? c->cap * CONV_GROW_FACTOR : CONV_INIT_CAP;
        message_t *tmp = realloc(c->msgs, (size_t)newcap * sizeof(message_t));
        if (!tmp)
            nc_fatal("out of memory (conversation_insert_at)");
        c->msgs = tmp;
        c->cap = newcap;
    }
    int tail = c->len - idx;
    if (tail > 0)
        memmove(&c->msgs[idx + 1], &c->msgs[idx], (size_t)tail * sizeof(message_t));
    c->len++;
    message_t *m = &c->msgs[idx];
    message_init(m, role);
    return m;
}

int conversation_drop_range(conversation_t *c, int start, int count) {
    if (!c || start < 0 || count <= 0 || start >= c->len)
        return 0;
    int end = start + count;
    if (end > c->len)
        end = c->len;
    int actual = end - start;
    for (int i = start; i < end; i++)
        message_free(&c->msgs[i]);
    int tail = c->len - end;
    if (tail > 0)
        memmove(&c->msgs[start], &c->msgs[end], (size_t)tail * sizeof(message_t));
    c->len -= actual;
    return actual;
}

int conversation_replace_head(conversation_t *c, int drop_count, const char *summary_text) {
    if (!c || drop_count <= 0 || drop_count > c->len)
        return c ? c->len : 0;
    int keep = c->len - drop_count;
    for (int i = 0; i < drop_count; i++)
        message_free(&c->msgs[i]);
    if (keep > 0)
        memmove(c->msgs + 1, c->msgs + drop_count, (size_t)keep * sizeof(message_t));
    message_init(&c->msgs[0], ROLE_USER);
    message_add_text(&c->msgs[0], summary_text ? summary_text : CONV_COMPACTED_FALLBACK_TEXT);
    c->len = keep + 1;
    return c->len;
}

/* ---- message helpers ---- */

void message_init(message_t *m, message_role_t role) {
    memset(m, 0, sizeof(*m));
    m->role = role;
}

static content_block_t *msg_grow(message_t *m) {
    if (m->num_blocks >= m->cap_blocks) {
        int newcap = m->cap_blocks ? m->cap_blocks * MSG_BLOCK_GROW_FACTOR : MSG_BLOCK_INIT_CAP;
        content_block_t *tmp = realloc(m->blocks, (size_t)newcap * sizeof(content_block_t));
        if (!tmp)
            nc_fatal("out of memory (msg_grow)");
        m->blocks = tmp;
        m->cap_blocks = newcap;
    }
    content_block_t *b = &m->blocks[m->num_blocks++];
    memset(b, 0, sizeof(*b));
    return b;
}

void message_add_text(message_t *m, const char *text) {
    content_block_t *b = msg_grow(m);
    b->type = CONTENT_TEXT;
    b->text = nc_xstrdup(text);
}

void message_add_image(message_t *m, const char *base64_data, const char *media_type) {
    content_block_t *b = msg_grow(m);
    b->type = CONTENT_IMAGE;
    b->image_data = nc_xstrdup(base64_data);
    b->image_media_type = nc_xstrdup(media_type);
}

void message_add_tool_use(message_t *m, const char *id, const char *name, cJSON *input) {
    content_block_t *b = msg_grow(m);
    b->type = CONTENT_TOOL_USE;
    b->tool_use_id = nc_xstrdup(id);
    b->tool_name = nc_xstrdup(name);
    b->tool_input = input ? cJSON_Duplicate(input, 1) : cJSON_CreateObject();
}

void message_add_tool_result(message_t *m, const char *id, const char *content, int is_error) {
    content_block_t *b = msg_grow(m);
    b->type = CONTENT_TOOL_RESULT;
    b->tool_use_id = nc_xstrdup(id);
    b->text = nc_xstrdup(content);
    b->is_error = is_error;
}

void message_free(message_t *m) {
    for (int i = 0; i < m->num_blocks; i++) {
        content_block_t *b = &m->blocks[i];
        free(b->text);
        free(b->tool_use_id);
        free(b->tool_name);
        if (b->tool_input)
            cJSON_Delete(b->tool_input);
        free(b->image_data);
        free(b->image_media_type);
    }
    free(m->blocks);
    memset(m, 0, sizeof(*m));
}
