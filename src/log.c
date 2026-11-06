#include "log.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#define KBM_TAG "keyboard_movement"
#define KBM_LINE_MAX 1024

static KbmHostLogFn g_host_log;
static FILE *g_file;
static KbmMutex g_lock;
static bool g_lock_ready;
static volatile int g_level = KBM_LOG_INFO;
static uint64_t g_start_ms;

static const char *const k_names[] = {"ERROR", "WARN ", "INFO ", "DEBUG", "TRACE"};

bool kbm_log_level_parse(const char *text, KbmLogLevel *out) {
    if (!text || !out) return false;
    char buf[16];
    size_t n = 0;
    while (*text && isspace((unsigned char)*text)) text++;
    while (text[n] && !isspace((unsigned char)text[n]) && n < sizeof buf - 1) {
        buf[n] = (char)tolower((unsigned char)text[n]);
        n++;
    }
    buf[n] = 0;
    static const struct {
        const char *name;
        KbmLogLevel level;
    } table[] = {{"error", KBM_LOG_ERROR}, {"warn", KBM_LOG_WARN}, {"warning", KBM_LOG_WARN}, {"info", KBM_LOG_INFO},
        {"debug", KBM_LOG_DEBUG}, {"trace", KBM_LOG_TRACE}};
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (!strcmp(buf, table[i].name)) {
            *out = table[i].level;
            return true;
        }
    }
    return false;
}

const char *kbm_log_level_name(KbmLogLevel level) {
    switch (level) {
    case KBM_LOG_ERROR: return "error";
    case KBM_LOG_WARN: return "warn";
    case KBM_LOG_INFO: return "info";
    case KBM_LOG_DEBUG: return "debug";
    case KBM_LOG_TRACE: return "trace";
    }
    return "info";
}

static void rotate_previous(const char *path) {
    FILE *probe = fopen(path, "rb");
    if (!probe) return;
    fclose(probe);
    char prev[1500];
    snprintf(prev, sizeof prev, "%s.prev", path);
    remove(prev);
    rename(path, prev);
}

void kbm_log_open(KbmHostLogFn host_log, const char *dir, const char *file_name, KbmLogLevel level, bool to_file) {
    if (!g_lock_ready) {
        kbm_mutex_init(&g_lock);
        g_lock_ready = true;
    }
    kbm_mutex_lock(&g_lock);
    g_host_log = host_log;
    g_level = level;
    g_start_ms = kbm_now_ms();
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
    if (to_file && dir && file_name) {
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", dir, file_name);
        rotate_previous(path);
        g_file = fopen(path, "wb");
    }
    kbm_mutex_unlock(&g_lock);
}

void kbm_log_set_level(KbmLogLevel level) { g_level = level; }

void kbm_log_file_close(void) {
    if (!g_lock_ready) return;
    kbm_mutex_lock(&g_lock);
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
    kbm_mutex_unlock(&g_lock);
}
KbmLogLevel kbm_log_level(void) { return (KbmLogLevel)g_level; }
bool kbm_log_file_active(void) { return g_file != NULL; }

void kbm_log(KbmLogLevel level, const char *fmt, ...) {
    if (!g_lock_ready) return;
    char msg[KBM_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    size_t len = strlen(msg);
    while (len && (msg[len - 1] == '\n' || msg[len - 1] == '\r')) msg[--len] = 0;

    uint64_t t = kbm_now_ms() - g_start_ms;
    kbm_mutex_lock(&g_lock);
    if (g_file && level <= (KbmLogLevel)g_level) {
        fprintf(g_file, "+%llu.%03llus T%04x %s %s\n", (unsigned long long)(t / 1000), (unsigned long long)(t % 1000),
            (unsigned)(kbm_thread_id() & 0xffff), k_names[level], msg);
        fflush(g_file);
    }
    if (g_host_log && level <= KBM_LOG_INFO && level <= (KbmLogLevel)g_level)
        g_host_log("[%s] %s %s\n", KBM_TAG, k_names[level], msg);
    kbm_mutex_unlock(&g_lock);
}

void kbm_log_detach_note(const char *text) {
    if (g_file) {
        fprintf(g_file, "%s\n", text);
        fflush(g_file);
    }
}

void kbm_log_close(void) {
    if (!g_lock_ready) return;
    kbm_mutex_lock(&g_lock);
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
    g_host_log = NULL;
    kbm_mutex_unlock(&g_lock);
}
