/*
 * mailbox.c — Inter-agent message passing with sequence tracking
 */

#include "nevercode.h"

/* MAILBOX_QUEUE_CAP in defaults.h */

typedef struct {
    char *from_agent;
    char *to_agent;
    char *content;
    long timestamp;
    uint64_t seq;
} mail_entry_t;

struct agent_mailbox {
    mail_entry_t queue[MAILBOX_QUEUE_CAP];
    int head;
    int tail;
    int count;
    uint64_t next_seq;
    pthread_mutex_t lock;
    char *owner_id;
};

static agent_mailbox_t g_mailboxes[MAX_LIVE_AGENTS];
static int g_mailbox_count = 0;
static pthread_mutex_t g_mailbox_registry_lock = PTHREAD_MUTEX_INITIALIZER;

void agent_mailbox_init(agent_mailbox_t *mb, const char *owner_id) {
    if (!mb)
        return;
    memset(mb, 0, sizeof(*mb));
    pthread_mutex_init(&mb->lock, NULL);
    mb->owner_id = owner_id ? nc_xstrdup(owner_id) : NULL;
    mb->next_seq = 1;
}

void agent_mailbox_free(agent_mailbox_t *mb) {
    if (!mb)
        return;
    pthread_mutex_lock(&mb->lock);
    for (int i = 0; i < mb->count; i++) {
        int idx = (mb->head + i) % MAILBOX_QUEUE_CAP;
        free(mb->queue[idx].from_agent);
        free(mb->queue[idx].to_agent);
        free(mb->queue[idx].content);
    }
    free(mb->owner_id);
    pthread_mutex_unlock(&mb->lock);
    pthread_mutex_destroy(&mb->lock);
    memset(mb, 0, sizeof(*mb));
}

uint64_t agent_mailbox_send(agent_mailbox_t *mb, const char *from, const char *content) {
    if (!mb || !content)
        return 0;
    pthread_mutex_lock(&mb->lock);
    if (mb->count >= MAILBOX_QUEUE_CAP) {
        int oldest = mb->head;
        free(mb->queue[oldest].from_agent);
        free(mb->queue[oldest].to_agent);
        free(mb->queue[oldest].content);
        mb->head = (mb->head + 1) % MAILBOX_QUEUE_CAP;
        mb->count--;
    }
    int slot = mb->tail;
    mb->queue[slot].from_agent = from ? nc_xstrdup(from) : NULL;
    mb->queue[slot].to_agent = mb->owner_id ? nc_xstrdup(mb->owner_id) : NULL;
    mb->queue[slot].content = nc_xstrdup(content);
    mb->queue[slot].timestamp = nc_wall_sec();
    mb->queue[slot].seq = mb->next_seq;
    uint64_t seq = mb->next_seq++;
    mb->tail = (mb->tail + 1) % MAILBOX_QUEUE_CAP;
    mb->count++;
    pthread_mutex_unlock(&mb->lock);
    return seq;
}

int agent_mailbox_recv(agent_mailbox_t *mb, char **from_out, char **content_out) {
    if (!mb)
        return -1;
    pthread_mutex_lock(&mb->lock);
    if (mb->count == 0) {
        pthread_mutex_unlock(&mb->lock);
        return -1;
    }
    int idx = mb->head;
    if (from_out)
        *from_out = mb->queue[idx].from_agent ? nc_xstrdup(mb->queue[idx].from_agent) : NULL;
    if (content_out)
        *content_out = mb->queue[idx].content ? nc_xstrdup(mb->queue[idx].content) : NULL;
    free(mb->queue[idx].from_agent);
    free(mb->queue[idx].to_agent);
    free(mb->queue[idx].content);
    memset(&mb->queue[idx], 0, sizeof(mail_entry_t));
    mb->head = (mb->head + 1) % MAILBOX_QUEUE_CAP;
    mb->count--;
    pthread_mutex_unlock(&mb->lock);
    return 0;
}

int agent_mailbox_peek(const agent_mailbox_t *mb) {
    return mb ? mb->count : 0;
}
uint64_t agent_mailbox_seq(const agent_mailbox_t *mb) {
    return mb ? mb->next_seq - 1 : 0;
}

int agent_mailbox_drain(agent_mailbox_t *mb, char **contents, int max) {
    if (!mb || !contents)
        return 0;
    pthread_mutex_lock(&mb->lock);
    int drained = 0;
    while (drained < max && mb->count > 0) {
        int idx = mb->head;
        contents[drained] = mb->queue[idx].content ? nc_xstrdup(mb->queue[idx].content) : NULL;
        free(mb->queue[idx].from_agent);
        free(mb->queue[idx].to_agent);
        free(mb->queue[idx].content);
        memset(&mb->queue[idx], 0, sizeof(mail_entry_t));
        mb->head = (mb->head + 1) % MAILBOX_QUEUE_CAP;
        mb->count--;
        drained++;
    }
    pthread_mutex_unlock(&mb->lock);
    return drained;
}

agent_mailbox_t *agent_mailbox_registry_create(const char *agent_id) {
    pthread_mutex_lock(&g_mailbox_registry_lock);
    if (g_mailbox_count >= MAX_LIVE_AGENTS) {
        pthread_mutex_unlock(&g_mailbox_registry_lock);
        return NULL;
    }
    agent_mailbox_t *mb = &g_mailboxes[g_mailbox_count++];
    agent_mailbox_init(mb, agent_id);
    pthread_mutex_unlock(&g_mailbox_registry_lock);
    return mb;
}

agent_mailbox_t *agent_mailbox_registry_find(const char *agent_id) {
    if (!agent_id)
        return NULL;
    pthread_mutex_lock(&g_mailbox_registry_lock);
    for (int i = 0; i < g_mailbox_count; i++) {
        if (g_mailboxes[i].owner_id && strcmp(g_mailboxes[i].owner_id, agent_id) == 0) {
            pthread_mutex_unlock(&g_mailbox_registry_lock);
            return &g_mailboxes[i];
        }
    }
    pthread_mutex_unlock(&g_mailbox_registry_lock);
    return NULL;
}

int agent_mailbox_registry_remove(const char *agent_id) {
    if (!agent_id)
        return -1;
    pthread_mutex_lock(&g_mailbox_registry_lock);
    for (int i = 0; i < g_mailbox_count; i++) {
        if (g_mailboxes[i].owner_id && strcmp(g_mailboxes[i].owner_id, agent_id) == 0) {
            agent_mailbox_free(&g_mailboxes[i]);
            if (i < g_mailbox_count - 1)
                g_mailboxes[i] = g_mailboxes[g_mailbox_count - 1];
            g_mailbox_count--;
            pthread_mutex_unlock(&g_mailbox_registry_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_mailbox_registry_lock);
    return -1;
}

void agent_mailbox_registry_clear(void) {
    pthread_mutex_lock(&g_mailbox_registry_lock);
    for (int i = 0; i < g_mailbox_count; i++)
        agent_mailbox_free(&g_mailboxes[i]);
    g_mailbox_count = 0;
    pthread_mutex_unlock(&g_mailbox_registry_lock);
}

int agent_mailbox_registry_count(void) {
    return g_mailbox_count;
}

char *agent_mailbox_format_status(const agent_mailbox_t *mb) {
    if (!mb)
        return nc_xstrdup("(null mailbox)");
    char buf[NC_LABEL_MAX];
    snprintf(buf, sizeof(buf), "mailbox[%s]: %d pending, seq=%" PRIu64,
             mb->owner_id ? mb->owner_id : "?", mb->count, mb->next_seq - 1);
    return nc_xstrdup(buf);
}
