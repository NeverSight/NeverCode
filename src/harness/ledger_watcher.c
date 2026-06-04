/*
 * ledger_watcher.c — Real-time event notification via ring-buffer watchers.
 *
 * External consumers subscribe to the event ledger and receive new
 * events via a bounded queue.  Each watcher maintains its own read
 * cursor so multiple consumers progress independently.  Overflow
 * drops the oldest unread events (lossy but non-blocking).
 */

#include "harness.h"

void ledger_watcher_init(ledger_watcher_t *w, int id) {
    memset(w, 0, sizeof(*w));
    w->watcher_id = id;
    w->active = true;
}

int ledger_subscribe(event_ledger_t *el, ledger_watcher_t *w) {
    if (!el || !w)
        return -1;
    if (el->nwatchers >= LEDGER_MAX_WATCHERS)
        return -1;

    for (int i = 0; i < el->nwatchers; i++)
        if (el->watchers[i] == w)
            return 0;

    el->watchers[el->nwatchers++] = w;
    w->active = true;
    return 0;
}

void ledger_unsubscribe(event_ledger_t *el, ledger_watcher_t *w) {
    if (!el || !w)
        return;
    w->active = false;

    for (int i = 0; i < el->nwatchers; i++) {
        if (el->watchers[i] == w) {
            for (int j = i; j < el->nwatchers - 1; j++)
                el->watchers[j] = el->watchers[j + 1];
            el->watchers[--el->nwatchers] = NULL;
            return;
        }
    }
}

static void _enqueue(ledger_watcher_t *w, const ledger_event_t *ev) {
    if (!w->active)
        return;

    if (w->count >= LEDGER_WATCHER_QUEUE) {
        w->head = (w->head + 1) % LEDGER_WATCHER_QUEUE;
        w->count--;
    }

    w->queue[w->tail] = *ev;
    w->tail = (w->tail + 1) % LEDGER_WATCHER_QUEUE;
    w->count++;
}

int ledger_notify_watchers(event_ledger_t *el, const ledger_event_t *ev) {
    if (!el || !ev)
        return -1;
    int notified = 0;

    for (int i = 0; i < el->nwatchers; i++) {
        if (el->watchers[i] && el->watchers[i]->active) {
            _enqueue(el->watchers[i], ev);
            notified++;
        }
    }
    return notified;
}

int ledger_watcher_poll(ledger_watcher_t *w, ledger_event_t *out, int max) {
    if (!w || !out || max <= 0)
        return 0;

    int n = 0;
    while (n < max && w->count > 0) {
        out[n++] = w->queue[w->head];
        w->head = (w->head + 1) % LEDGER_WATCHER_QUEUE;
        w->count--;
    }
    return n;
}

bool ledger_watcher_has_events(const ledger_watcher_t *w) {
    return w && w->count > 0;
}

int ledger_watcher_pending(const ledger_watcher_t *w) {
    return w ? w->count : 0;
}
