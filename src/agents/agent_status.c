/*
 * agent_status.c — human-readable names for agent_status_t
 */

#include "nevercode.h"

const char *agent_status_name(agent_status_t s) {
    switch (s) {
    case AGENT_STATUS_RUNNING:
        return "running";
    case AGENT_STATUS_COMPLETED:
        return "completed";
    case AGENT_STATUS_FAILED:
        return "failed";
    case AGENT_STATUS_PAUSED:
        return "paused";
    default:
        return "unknown";
    }
}
