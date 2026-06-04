/*
 * keybindings.c — Keyboard shortcut registry and dispatch
 *
 * Default bindings for common REPL actions (submit, cancel, clear,
 * history navigation, tab completion, vim toggle, effort cycling).
 * Supports custom bindings loaded from JSON config.
 */

#include "nevercode.h"
#include "defaults.h"

typedef enum {
    K_NONE = 0,
    K_SUBMIT,
    K_CANCEL,
    K_CLEAR,
    K_HP,
    K_HN,
    K_COMP,
    K_ESC,
    K_VIM,
    K_EFF,
    K_COMPACT,
    K_UNDO,
    K_HELP,
    K_INT,
    K_CUSTOM
} kb_action_t;

typedef struct {
    int key;
    int mod;
    kb_action_t act;
    char cmd[KB_CUSTOM_CMD_MAX];
    const char *desc;
    bool on;
} kb_entry_t;

static struct {
    kb_entry_t entries[KB_MAX_BINDINGS];
    int count;
    bool initialized;
    int dispatch_count;
} g_kb;

static const struct {
    int key;
    int mod;
    kb_action_t act;
    const char *desc;
} defaults[] = {
    {'\n', 0, K_SUBMIT, "Submit"}, {'c', 1, K_INT, "Interrupt"}, {'l', 1, K_CLEAR, "Clear"},
    {'p', 1, K_HP, "Prev"},        {'n', 1, K_HN, "Next"},       {'\t', 0, K_COMP, "Tab"},
    {27, 0, K_ESC, "Escape"},      {'v', 1, K_VIM, "Vim"},       {'e', 1, K_EFF, "Effort"},
    {'u', 1, K_UNDO, "Undo"},      {0, 0, K_NONE, NULL}};

void keybindings_init(void) {
    memset(&g_kb, 0, sizeof(g_kb));
    for (int i = 0; defaults[i].desc; i++) {
        if (g_kb.count >= KB_MAX_BINDINGS)
            break;
        kb_entry_t *b = &g_kb.entries[g_kb.count];
        b->key = defaults[i].key;
        b->mod = defaults[i].mod;
        b->act = defaults[i].act;
        b->desc = defaults[i].desc;
        b->on = true;
        g_kb.count++;
    }
    g_kb.initialized = true;
}

void keybindings_free(void) {
    memset(&g_kb, 0, sizeof(g_kb));
}

int keybinding_remove(int key, int mod) {
    for (int i = 0; i < g_kb.count; i++) {
        if (g_kb.entries[i].key == key && g_kb.entries[i].mod == mod) {
            memmove(&g_kb.entries[i], &g_kb.entries[i + 1],
                    (size_t)(g_kb.count - i - 1) * sizeof(kb_entry_t));
            g_kb.count--;
            return 0;
        }
    }
    return -1;
}

int keybinding_count(void) {
    return g_kb.count;
}

static const char *key_name(int key, int mod) {
    static char buf[NC_SHORT_MAX];
    int off = 0;

    if (mod & 1)
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "C-");

    if (key == '\n')
        snprintf(buf + off, sizeof(buf) - (size_t)off, "Enter");
    else if (key == '\t')
        snprintf(buf + off, sizeof(buf) - (size_t)off, "Tab");
    else if (key == 27)
        snprintf(buf + off, sizeof(buf) - (size_t)off, "Esc");
    else if (key >= 32 && key < 127)
        snprintf(buf + off, sizeof(buf) - (size_t)off, "%c", key);
    else
        snprintf(buf + off, sizeof(buf) - (size_t)off, "0x%02x", key);

    return buf;
}

static const char *action_name(kb_action_t a) {
    switch (a) {
    case K_NONE:
        return "none";
    case K_SUBMIT:
        return "submit";
    case K_CANCEL:
        return "cancel";
    case K_CLEAR:
        return "clear";
    case K_HP:
        return "hp";
    case K_HN:
        return "hn";
    case K_COMP:
        return "comp";
    case K_ESC:
        return "esc";
    case K_VIM:
        return "vim";
    case K_EFF:
        return "eff";
    case K_COMPACT:
        return "compact";
    case K_UNDO:
        return "undo";
    case K_HELP:
        return "help";
    case K_INT:
        return "int";
    case K_CUSTOM:
        return "custom";
    }
    return "?";
}

char *keybindings_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Keybindings: %d total, %d dispatches\n", g_kb.count, g_kb.dispatch_count);

    for (int i = 0; i < g_kb.count; i++)
        strbuf_appendf(
            &sb, "  %-10s -> %-10s %s\n", key_name(g_kb.entries[i].key, g_kb.entries[i].mod),
            action_name(g_kb.entries[i].act), g_kb.entries[i].desc ? g_kb.entries[i].desc : "");

    return strbuf_detach(&sb);
}
