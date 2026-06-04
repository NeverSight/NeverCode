/*
 * file_watcher.c — Cross-platform file change monitoring
 *
 * Watches config files (project docs, mcp.json, .rules) and skills
 * directories for changes. Triggers callbacks so the main loop can hot-reload
 * without restarting.
 *
 * macOS: kqueue/kevent
 * Linux: inotify
 * Fallback: stat()-based polling via file_watcher_check_poll()
 */

#include "nevercode.h"

#ifdef __APPLE__
#include <sys/event.h>
#include <sys/time.h>
#define HAS_KQUEUE 1
#define HAS_INOTIFY 0
#elif defined(__linux__)
#include <sys/inotify.h>
#include <poll.h>

#define HAS_KQUEUE 0
#define HAS_INOTIFY 1
#else
#define HAS_KQUEUE 0
#define HAS_INOTIFY 0
#endif

static file_watcher_t g_watcher;
static bool g_watcher_initialized = false;

void file_watcher_init(file_watcher_t *w) {
    memset(w, 0, sizeof(*w));
    w->kq_fd = -1;
    w->running = false;
    w->num_watches = 0;
    w->num_callbacks = 0;
    w->debounce_ms = FILE_WATCHER_DEBOUNCE_MS;
}

void file_watcher_free(file_watcher_t *w) {
    file_watcher_stop(w);
    for (int i = 0; i < w->num_watches; i++) {
        if (w->watches[i].fd >= 0)
            close(w->watches[i].fd);
        free(w->watches[i].path);
    }
    w->num_watches = 0;
    w->num_callbacks = 0;
    if (w->kq_fd >= 0) {
        close(w->kq_fd);
        w->kq_fd = -1;
    }
}

int file_watcher_add(file_watcher_t *w, const char *path, file_watch_type_t type) {
    if (!path || w->num_watches >= MAX_WATCHES)
        return -1;

    struct stat st;
    if (stat(path, &st) != 0)
        return -1;

    if (type == WATCH_DIR && !S_ISDIR(st.st_mode))
        return -1;
    if (type == WATCH_FILE && !S_ISREG(st.st_mode))
        return -1;

    for (int i = 0; i < w->num_watches; i++) {
        if (strcmp(w->watches[i].path, path) == 0)
            return i;
    }

#if HAS_KQUEUE
    int fd = open(path, O_RDONLY | O_EVTONLY);
#else
    int fd = open(path, O_RDONLY);
#endif
    if (fd < 0)
        return -1;

    int idx = w->num_watches++;
    w->watches[idx].path = nc_xstrdup(path);
    w->watches[idx].fd = fd;
    w->watches[idx].type = type;
    w->watches[idx].last_mtime = st.st_mtime;

    return idx;
}

int file_watcher_add_callback(file_watcher_t *w, file_watch_cb cb, void *ctx) {
    if (!cb || w->num_callbacks >= MAX_WATCH_CALLBACKS)
        return -1;
    int idx = w->num_callbacks++;
    w->callbacks[idx] = cb;
    w->callback_ctx[idx] = ctx;
    return idx;
}

static void fire_callbacks(file_watcher_t *w, const char *path, file_watch_type_t type) {
    for (int i = 0; i < w->num_callbacks; i++) {
        if (w->callbacks[i])
            w->callbacks[i](path, type, w->callback_ctx[i]);
    }
}

#if HAS_KQUEUE
static void *watcher_thread_kqueue(void *arg) {
    file_watcher_t *w = arg;
    struct kevent events[MAX_WATCHES];
    struct timespec timeout = {.tv_sec = 0, .tv_nsec = FILE_WATCHER_DEBOUNCE_MS * 1000L * 1000L};

    while (w->running) {
        int nev = kevent(w->kq_fd, NULL, 0, events, w->num_watches, &timeout);
        if (nev < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        for (int i = 0; i < nev; i++) {
            int fd = (int)events[i].ident;
            for (int j = 0; j < w->num_watches; j++) {
                if (w->watches[j].fd == fd) {
                    struct stat st;
                    if (fstat(fd, &st) == 0 && st.st_mtime != w->watches[j].last_mtime) {
                        w->watches[j].last_mtime = st.st_mtime;
                        fire_callbacks(w, w->watches[j].path, w->watches[j].type);
                    } else if (events[i].fflags & (NOTE_DELETE | NOTE_RENAME)) {
                        close(w->watches[j].fd);
                        w->watches[j].fd = open(w->watches[j].path, O_RDONLY | O_EVTONLY);
                        if (w->watches[j].fd >= 0) {
                            struct kevent ke;
                            EV_SET(&ke, w->watches[j].fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
                                   NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, NULL);
                            kevent(w->kq_fd, &ke, 1, NULL, 0, NULL);
                        }
                        fire_callbacks(w, w->watches[j].path, w->watches[j].type);
                    }
                    break;
                }
            }
        }
    }
    return NULL;
}
#endif

#if HAS_INOTIFY
static int find_watch_by_wd(file_watcher_t *w, int wd) {
    for (int i = 0; i < w->num_watches; i++)
        if (w->watches[i].fd == wd)
            return i;
    return -1;
}

static void *watcher_thread_inotify(void *arg) {
    file_watcher_t *w = arg;
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1]
        __attribute__((aligned(__alignof__(struct inotify_event))));

    while (w->running) {
        struct pollfd pfd = {.fd = w->kq_fd, .events = POLLIN};
        int pr = poll(&pfd, 1, FILE_WATCHER_DEBOUNCE_MS);
        if (pr <= 0)
            continue;

        ssize_t len = read(w->kq_fd, buf, sizeof(buf));
        if (len <= 0)
            continue;

        for (char *ptr = buf; ptr < buf + len;) {
            struct inotify_event *ev = (struct inotify_event *)ptr;
            int idx = find_watch_by_wd(w, ev->wd);
            if (idx >= 0) {
                struct stat st;
                if (stat(w->watches[idx].path, &st) == 0 &&
                    st.st_mtime != w->watches[idx].last_mtime) {
                    w->watches[idx].last_mtime = st.st_mtime;
                }
                fire_callbacks(w, w->watches[idx].path, w->watches[idx].type);
            }
            ptr += sizeof(struct inotify_event) + ev->len;
        }
    }
    return NULL;
}
#endif

/* Polling-based fallback thread for platforms without native support */
#if !HAS_KQUEUE && !HAS_INOTIFY
static void *watcher_thread_poll(void *arg) {
    file_watcher_t *w = arg;
    while (w->running) {
        usleep(FILE_WATCHER_DEBOUNCE_MS * 1000u);
        file_watcher_check_poll(w);
    }
    return NULL;
}
#endif

int file_watcher_start(file_watcher_t *w) {
    if (w->running || w->num_watches == 0)
        return -1;

#if HAS_KQUEUE
    w->kq_fd = kqueue();
    if (w->kq_fd < 0)
        return -1;

    for (int i = 0; i < w->num_watches; i++) {
        if (w->watches[i].fd < 0)
            continue;
        struct kevent ke;
        EV_SET(&ke, w->watches[i].fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
               NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, NULL);
        if (kevent(w->kq_fd, &ke, 1, NULL, 0, NULL) < 0) {
            close(w->kq_fd);
            w->kq_fd = -1;
            return -1;
        }
    }

    w->running = true;
    if (pthread_create(&w->thread, NULL, watcher_thread_kqueue, w) != 0) {
        w->running = false;
        close(w->kq_fd);
        w->kq_fd = -1;
        return -1;
    }
    return 0;

#elif HAS_INOTIFY
    w->kq_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (w->kq_fd < 0)
        return -1;

    uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE_SELF | IN_ATTRIB;
    for (int i = 0; i < w->num_watches; i++) {
        if (w->watches[i].fd >= 0)
            close(w->watches[i].fd);
        int wd = inotify_add_watch(w->kq_fd, w->watches[i].path, mask);
        if (wd < 0) {
            close(w->kq_fd);
            w->kq_fd = -1;
            return -1;
        }
        w->watches[i].fd = wd;
    }

    w->running = true;
    if (pthread_create(&w->thread, NULL, watcher_thread_inotify, w) != 0) {
        w->running = false;
        close(w->kq_fd);
        w->kq_fd = -1;
        return -1;
    }
    return 0;

#else
    w->running = true;
    if (pthread_create(&w->thread, NULL, watcher_thread_poll, w) != 0) {
        w->running = false;
        return -1;
    }
    return 0;
#endif
}

void file_watcher_stop(file_watcher_t *w) {
    if (!w->running)
        return;
    w->running = false;
    pthread_join(w->thread, NULL);
}

bool file_watcher_active(const file_watcher_t *w) {
    return w->running;
}

int file_watcher_count(const file_watcher_t *w) {
    return w->num_watches;
}

const char *file_watcher_path(const file_watcher_t *w, int idx) {
    if (idx < 0 || idx >= w->num_watches)
        return NULL;
    return w->watches[idx].path;
}

int file_watcher_check_poll(file_watcher_t *w) {
    int changed = 0;
    for (int i = 0; i < w->num_watches; i++) {
        struct stat st;
        if (stat(w->watches[i].path, &st) == 0 && st.st_mtime != w->watches[i].last_mtime) {
            w->watches[i].last_mtime = st.st_mtime;
            fire_callbacks(w, w->watches[i].path, w->watches[i].type);
            changed++;
        }
    }
    return changed;
}

file_watcher_t *file_watcher_global(void) {
    if (!g_watcher_initialized) {
        file_watcher_init(&g_watcher);
        g_watcher_initialized = true;
    }
    return &g_watcher;
}

static int add_existing_watch(file_watcher_t *w, const char *project_root, const char *relative) {
    char path[NC_PATH_MAX];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", project_root, relative);
    if (stat(path, &st) != 0)
        return 0;
    file_watch_type_t t = S_ISDIR(st.st_mode) ? WATCH_DIR : WATCH_FILE;
    return file_watcher_add(w, path, t) >= 0 ? 1 : 0;
}

int file_watcher_add_config_paths(file_watcher_t *w, const char *project_root) {
    int added = 0;
    for (int i = 0; i < nc_project_doc_agents_n; i++)
        added += add_existing_watch(w, project_root, nc_project_doc_agents_paths[i]);
    for (int i = 0; i < nc_project_doc_extra_n; i++)
        added += add_existing_watch(w, project_root, nc_project_doc_extra_paths[i]);

    const char *config_names[] = {CONFIG_DOTDIR_COMPAT "/settings.json",
                                  CONFIG_DOTDIR_PRIMARY "/settings.json",
                                  CONFIG_DOTDIR_SECONDARY "/settings.json",
                                  CONFIG_DOTDIR_COMPAT "/mcp.json",
                                  CONFIG_DOTDIR_PRIMARY "/mcp.json",
                                  CONFIG_DOTDIR_SECONDARY "/mcp.json",
                                  CONFIG_DOTDIR_COMPAT "/rules",
                                  CONFIG_DOTDIR_PRIMARY "/rules",
                                  CONFIG_DOTDIR_SECONDARY "/rules",
                                  NULL};

    for (int i = 0; config_names[i]; i++)
        added += add_existing_watch(w, project_root, config_names[i]);

    const char *skill_dirs[] = {CONFIG_DOTDIR_COMPAT "/skills", CONFIG_DOTDIR_PRIMARY "/skills",
                                CONFIG_DOTDIR_SECONDARY "/skills", NULL};
    for (int i = 0; skill_dirs[i]; i++)
        added += add_existing_watch(w, project_root, skill_dirs[i]);

    return added;
}
