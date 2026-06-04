#include "nevercode.h"
#include <sys/statvfs.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#endif

typedef struct {
    long ts;
    double cpu_pct;
    long mem_used_mb, mem_total_mb, disk_free_mb;
} rsample_t;

typedef struct {
    rsample_t samples[RMON_SAMPLE_MAX];
    int count, head;
    bool running;
    long last_poll, start_time;
    int poll_count;
    double peak_cpu;
    long peak_mem_mb, low_disk_mb;
} rmon_t;

static rmon_t g_rmon;

void resource_monitor_init(void) {
    memset(&g_rmon, 0, sizeof(g_rmon));
    g_rmon.start_time = nc_wall_sec();
    g_rmon.running = true;
    g_rmon.low_disk_mb = LONG_MAX;
}

void resource_monitor_free(void) {
    g_rmon.running = false;
}

static void push_sample(const rsample_t *s) {
    g_rmon.samples[g_rmon.head] = *s;
    g_rmon.head = (g_rmon.head + 1) % RMON_SAMPLE_MAX;
    if (g_rmon.count < RMON_SAMPLE_MAX)
        g_rmon.count++;
    if (s->cpu_pct > g_rmon.peak_cpu)
        g_rmon.peak_cpu = s->cpu_pct;
    if (s->mem_used_mb > g_rmon.peak_mem_mb)
        g_rmon.peak_mem_mb = s->mem_used_mb;
    if (s->disk_free_mb >= 0 && s->disk_free_mb < g_rmon.low_disk_mb)
        g_rmon.low_disk_mb = s->disk_free_mb;
}

static double get_cpu(void) {
#ifdef __APPLE__
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t cnt = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&info, &cnt) !=
        KERN_SUCCESS)
        return -1.0;
    unsigned int tot = 0;
    for (int i = 0; i < CPU_STATE_MAX; i++)
        tot += info.cpu_ticks[i];
    if (tot == 0)
        return 0.0;
    return 100.0 * (1.0 - (double)info.cpu_ticks[CPU_STATE_IDLE] / (double)tot);
#elif defined(__linux__)
    static long prev_total = 0, prev_idle = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp)
        return -1.0;
    char line[NC_LABEL_MAX];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1.0;
    }
    fclose(fp);
    long user, nice, sys, idle, iowait, irq, softirq, steal;
    if (sscanf(line, "cpu %ld %ld %ld %ld %ld %ld %ld %ld", &user, &nice, &sys, &idle, &iowait,
               &irq, &softirq, &steal) < 4)
        return -1.0;
    long total = user + nice + sys + idle + iowait + irq + softirq + steal;
    long idle_all = idle + iowait;
    double pct = -1.0;
    if (prev_total > 0) {
        long dt = total - prev_total;
        long di = idle_all - prev_idle;
        if (dt > 0)
            pct = 100.0 * (1.0 - (double)di / (double)dt);
    }
    prev_total = total;
    prev_idle = idle_all;
    return pct;
#else
    return -1.0;
#endif
}

static void get_mem(long *used, long *total) {
#ifdef __APPLE__
    mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm, &cnt) !=
        KERN_SUCCESS) {
        *used = 0;
        *total = 0;
        return;
    }
    vm_size_t ps;
    host_page_size(mach_host_self(), &ps);
    long pu = (long)(vm.active_count + vm.wire_count);
    long pt = pu + (long)(vm.inactive_count + vm.free_count + vm.speculative_count);
    *used = pu * (long)ps / (1024 * 1024);
    *total = pt * (long)ps / (1024 * 1024);
#elif defined(__linux__)
    *used = 0;
    *total = 0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp)
        return;
    long mem_total = 0, mem_avail = 0;
    char line[NC_TAG_MAX];
    while (fgets(line, sizeof(line), fp)) {
        long val;
        if (sscanf(line, "MemTotal: %ld kB", &val) == 1)
            mem_total = val;
        else if (sscanf(line, "MemAvailable: %ld kB", &val) == 1)
            mem_avail = val;
        if (mem_total > 0 && mem_avail > 0)
            break;
    }
    fclose(fp);
    *total = mem_total / 1024;
    *used = (mem_total - mem_avail) / 1024;
#else
    *used = 0;
    *total = 0;
#endif
}

int resource_monitor_poll(void) {
    if (!g_rmon.running)
        return -1;
    long now = nc_wall_sec();
    if (now - g_rmon.last_poll < RMON_POLL_INTERVAL_S && g_rmon.poll_count > 0)
        return 0;
    rsample_t s;
    s.ts = now;
    s.cpu_pct = get_cpu();
    get_mem(&s.mem_used_mb, &s.mem_total_mb);
    struct statvfs vfs;
    s.disk_free_mb =
        (statvfs(".", &vfs) == 0) ? (long)(vfs.f_bavail * vfs.f_frsize / (1024 * 1024)) : -1;
    push_sample(&s);
    g_rmon.last_poll = now;
    g_rmon.poll_count++;
    return 1;
}

bool resource_monitor_low_memory(int pct) {
    if (g_rmon.count == 0)
        return false;
    int i = (g_rmon.head - 1 + RMON_SAMPLE_MAX) % RMON_SAMPLE_MAX;
    rsample_t *s = &g_rmon.samples[i];
    return s->mem_total_mb > 0 && (int)(s->mem_used_mb * 100 / s->mem_total_mb) >= pct;
}

bool resource_monitor_low_disk(long min) {
    if (g_rmon.count == 0)
        return false;
    int i = (g_rmon.head - 1 + RMON_SAMPLE_MAX) % RMON_SAMPLE_MAX;
    return g_rmon.samples[i].disk_free_mb >= 0 && g_rmon.samples[i].disk_free_mb < min;
}

double resource_monitor_avg_cpu(void) {
    if (g_rmon.count == 0)
        return 0.0;
    double sum = 0;
    for (int i = 0; i < g_rmon.count; i++)
        sum += g_rmon.samples[i].cpu_pct;
    return sum / g_rmon.count;
}

int resource_monitor_sample_count(void) {
    return g_rmon.count;
}

char *resource_monitor_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    long up = nc_wall_sec() - g_rmon.start_time;
    strbuf_appendf(&sb,
                   "Resource Monitor: %d samples over %lds\n"
                   "  CPU: avg=%.1f%%, peak=%.1f%%\n"
                   "  Memory: peak=%ldMB\n"
                   "  Disk: lowest free=%ldMB\n",
                   g_rmon.count, up, resource_monitor_avg_cpu(), g_rmon.peak_cpu,
                   g_rmon.peak_mem_mb, g_rmon.low_disk_mb == LONG_MAX ? 0 : g_rmon.low_disk_mb);
    return strbuf_detach(&sb);
}
