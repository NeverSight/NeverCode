/*
 * agent_v2.c — Multi-agent v2 protocol
 *
 * Extended multi-agent operations including:
 * - Follow-up tasks from completed agents
 * - Inter-agent messaging with typed payloads
 * - Agent listing with status and metadata
 * - Graceful agent shutdown with result collection
 */

#include "nevercode.h"
#include "repl.h"

/* Macros sourced from defaults.h: AV2_MAX_AGENTS, AV2_MAX_MESSAGES, AV2_MSG_MAX_LEN */
#define AV2_TASK_DESC_MAX COORD_TASK_DESC_MAX

typedef enum {
    AV2_MSG_TEXT,
    AV2_MSG_RESULT,
    AV2_MSG_STATUS,
    AV2_MSG_FOLLOWUP,
    AV2_MSG_SHUTDOWN
} av2_msg_kind_t;

typedef struct {
    av2_msg_kind_t kind;
    char from[NC_FMT_MAX];
    char to[NC_FMT_MAX];
    char payload[AV2_MSG_MAX_LEN];
    time_t timestamp;
} av2_message_t;

typedef enum {
    AV2_AGENT_IDLE,
    AV2_AGENT_RUNNING,
    AV2_AGENT_WAITING,
    AV2_AGENT_DONE,
    AV2_AGENT_FAILED
} av2_agent_state_t;

typedef struct {
    char id[NC_FMT_MAX];
    char task_desc[AV2_TASK_DESC_MAX];
    av2_agent_state_t state;
    int turn_count;
    time_t started_at;
    time_t finished_at;
    bool has_followup;
    char followup_desc[AV2_TASK_DESC_MAX];
} av2_agent_info_t;

typedef struct {
    av2_agent_info_t agents[AV2_MAX_AGENTS];
    int count;
    av2_message_t inbox[AV2_MAX_MESSAGES];
    int inbox_count;
} av2_registry_t;

static av2_registry_t g_av2;

void av2_init(void) {
    memset(&g_av2, 0, sizeof(g_av2));
}

static int find_agent(const char *id) {
    for (int i = 0; i < g_av2.count; i++)
        if (strcmp(g_av2.agents[i].id, id) == 0)
            return i;
    return -1;
}

int av2_spawn(const char *id, const char *task_desc) {
    if (!id || g_av2.count >= AV2_MAX_AGENTS)
        return -1;
    if (find_agent(id) >= 0)
        return -1;

    av2_agent_info_t *a = &g_av2.agents[g_av2.count];
    memset(a, 0, sizeof(*a));
    nc_strlcpy(a->id, id, sizeof(a->id));
    if (task_desc)
        nc_strlcpy(a->task_desc, task_desc, sizeof(a->task_desc));
    a->state = AV2_AGENT_RUNNING;
    a->started_at = nc_time_now();
    g_av2.count++;
    return 0;
}

int av2_send_message(const char *from, const char *to, av2_msg_kind_t kind, const char *payload) {
    if (g_av2.inbox_count >= AV2_MAX_MESSAGES)
        return -1;

    av2_message_t *m = &g_av2.inbox[g_av2.inbox_count];
    memset(m, 0, sizeof(*m));
    m->kind = kind;
    if (from)
        nc_strlcpy(m->from, from, sizeof(m->from));
    if (to)
        nc_strlcpy(m->to, to, sizeof(m->to));
    if (payload)
        nc_strlcpy(m->payload, payload, sizeof(m->payload));
    m->timestamp = nc_time_now();
    g_av2.inbox_count++;
    return 0;
}

int av2_receive_messages(const char *agent_id, av2_message_t *out, int max_msgs) {
    if (!agent_id || !out || max_msgs <= 0)
        return 0;

    int received = 0;
    for (int i = 0; i < g_av2.inbox_count && received < max_msgs; i++) {
        if (strcmp(g_av2.inbox[i].to, agent_id) == 0) {
            out[received++] = g_av2.inbox[i];
            if (i < g_av2.inbox_count - 1)
                memmove(&g_av2.inbox[i], &g_av2.inbox[i + 1],
                        (size_t)(g_av2.inbox_count - i - 1) * sizeof(av2_message_t));
            g_av2.inbox_count--;
            i--;
        }
    }
    return received;
}

int av2_set_state(const char *id, av2_agent_state_t state) {
    int idx = find_agent(id);
    if (idx < 0)
        return -1;
    g_av2.agents[idx].state = state;
    if (state == AV2_AGENT_DONE || state == AV2_AGENT_FAILED)
        g_av2.agents[idx].finished_at = nc_time_now();
    return 0;
}

int av2_add_followup(const char *id, const char *followup_desc) {
    int idx = find_agent(id);
    if (idx < 0)
        return -1;
    g_av2.agents[idx].has_followup = true;
    if (followup_desc)
        nc_strlcpy(g_av2.agents[idx].followup_desc, followup_desc,
                   sizeof(g_av2.agents[idx].followup_desc));
    return 0;
}

int av2_close(const char *id) {
    int idx = find_agent(id);
    if (idx < 0)
        return -1;
    g_av2.agents[idx].state = AV2_AGENT_DONE;
    g_av2.agents[idx].finished_at = nc_time_now();
    return 0;
}

int av2_wait(const char *id, int timeout_ms) {
    int idx = find_agent(id);
    if (idx < 0)
        return -1;
    (void)timeout_ms;
    return (g_av2.agents[idx].state == AV2_AGENT_DONE ||
            g_av2.agents[idx].state == AV2_AGENT_FAILED)
               ? 0
               : 1;
}

int av2_count(void) {
    return g_av2.count;
}

int av2_active_count(void) {
    int n = 0;
    for (int i = 0; i < g_av2.count; i++)
        if (g_av2.agents[i].state == AV2_AGENT_RUNNING ||
            g_av2.agents[i].state == AV2_AGENT_WAITING)
            n++;
    return n;
}

char *av2_list_agents(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Agents (%d total, %d active):\n", g_av2.count, av2_active_count());
    for (int i = 0; i < g_av2.count; i++) {
        const av2_agent_info_t *a = &g_av2.agents[i];
        const char *state_names[] = {"idle", "running", "waiting", "done", "failed"};
        strbuf_appendf(&sb, "  [%s] %s — turns=%d%s\n", a->id, state_names[a->state], a->turn_count,
                       a->has_followup ? " (has followup)" : "");
        if (a->task_desc[0])
            strbuf_appendf(&sb, "    task: %s\n", a->task_desc);
    }
    return strbuf_detach(&sb);
}

int av2_increment_turns(const char *id) {
    int idx = find_agent(id);
    if (idx < 0)
        return -1;
    g_av2.agents[idx].turn_count++;
    return g_av2.agents[idx].turn_count;
}

const char *av2_msg_kind_name(av2_msg_kind_t k) {
    switch (k) {
    case AV2_MSG_TEXT:
        return "text";
    case AV2_MSG_RESULT:
        return "result";
    case AV2_MSG_STATUS:
        return "status";
    case AV2_MSG_FOLLOWUP:
        return "followup";
    case AV2_MSG_SHUTDOWN:
        return "shutdown";
    default:
        return "unknown";
    }
}
