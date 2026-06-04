/*
 * input_history.c — Persistent input history across sessions.
 *
 * Saves user inputs to ~/.agents/input_history and reloads them on
 * startup so arrow-key navigation spans previous sessions.
 * Deduplicates consecutive identical entries.
 */

#include "nevercode.h"

/* NC_NC_INPUT_HIST_MAX in defaults.h */
/* NC_INPUT_HIST_FILENAME in defaults.h */
#define INPUT_HIST_FILE NC_INPUT_HIST_FILENAME

static struct {
    char *items[NC_INPUT_HIST_MAX];
    int count;
    int cursor;
    bool loaded;
} g_ih;

void input_history_init(void) {
    memset(&g_ih, 0, sizeof(g_ih));
    g_ih.cursor = -1;
}

void input_history_free(void) {
    for (int i = 0; i < g_ih.count; i++)
        free(g_ih.items[i]);
    memset(&g_ih, 0, sizeof(g_ih));
    g_ih.cursor = -1;
}

void input_history_push(const char *text) {
    if (!text || !text[0])
        return;

    while (*text == ' ' || *text == '\t')
        text++;
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\n'))
        len--;
    if (len == 0)
        return;

    if (g_ih.count > 0) {
        const char *last = g_ih.items[g_ih.count - 1];
        if (strlen(last) == len && strncmp(last, text, len) == 0)
            return;
    }

    if (g_ih.count >= NC_INPUT_HIST_MAX) {
        free(g_ih.items[0]);
        memmove(g_ih.items, g_ih.items + 1, (size_t)(g_ih.count - 1) * sizeof(char *));
        g_ih.count--;
    }

    char *dup = malloc(len + 1);
    if (!dup)
        return;
    memcpy(dup, text, len);
    dup[len] = '\0';
    g_ih.items[g_ih.count++] = dup;
    g_ih.cursor = -1;
}

const char *input_history_up(void) {
    if (g_ih.count == 0)
        return NULL;
    if (g_ih.cursor < 0)
        g_ih.cursor = g_ih.count - 1;
    else if (g_ih.cursor > 0)
        g_ih.cursor--;
    return g_ih.items[g_ih.cursor];
}

const char *input_history_down(void) {
    if (g_ih.cursor < 0)
        return NULL;
    g_ih.cursor++;
    if (g_ih.cursor >= g_ih.count) {
        g_ih.cursor = -1;
        return NULL;
    }
    return g_ih.items[g_ih.cursor];
}

void input_history_reset_cursor(void) {
    g_ih.cursor = -1;
}

int input_history_count(void) {
    return g_ih.count;
}

int input_history_save(void) {
    char path[NC_PATH_MAX];
    config_home_path(path, sizeof(path), INPUT_HIST_FILE);

    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    int start = g_ih.count > NC_INPUT_HIST_MAX ? g_ih.count - NC_INPUT_HIST_MAX : 0;
    int err = 0;
    for (int i = start; i < g_ih.count && !err; i++) {
        if (fputs(g_ih.items[i], f) == EOF || fputc('\n', f) == EOF)
            err = -1;
    }
    if (!err && fflush(f))
        err = -1;
    fclose(f);
    return err;
}

int input_history_load(void) {
    char path[NC_PATH_MAX];
    config_home_path(path, sizeof(path), INPUT_HIST_FILE);

    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    char line[NC_CMD_MAX];
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len > 0) {
            input_history_push(line);
            loaded++;
        }
    }
    fclose(f);
    g_ih.loaded = true;
    return loaded;
}
