#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum KbmLogLevel {
    KBM_LOG_ERROR = 0,
    KBM_LOG_WARN,
    KBM_LOG_INFO,
    KBM_LOG_DEBUG,
    KBM_LOG_TRACE
} KbmLogLevel;

typedef void (*KbmHostLogFn)(const char *fmt, ...);

bool kbm_log_level_parse(const char *text, KbmLogLevel *out);
const char *kbm_log_level_name(KbmLogLevel level);
void kbm_log_open(KbmHostLogFn host_log, const char *dir, const char *file_name, KbmLogLevel level, bool to_file);
void kbm_log_set_level(KbmLogLevel level);
void kbm_log_file_close(void);
KbmLogLevel kbm_log_level(void);
bool kbm_log_file_active(void);
void kbm_log(KbmLogLevel level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void kbm_log_detach_note(const char *text);
void kbm_log_close(void);

#define KBM_LOG(level, ...)                                  \
    do {                                                     \
        if ((level) <= kbm_log_level()) kbm_log((level), __VA_ARGS__); \
    } while (0)
