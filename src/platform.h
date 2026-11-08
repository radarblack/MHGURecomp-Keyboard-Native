#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION KbmMutex;
#else
#include <pthread.h>
typedef pthread_mutex_t KbmMutex;
#endif

void kbm_mutex_init(KbmMutex *m);
void kbm_mutex_lock(KbmMutex *m);
void kbm_mutex_unlock(KbmMutex *m);
uint64_t kbm_now_ms(void);
uint32_t kbm_thread_id(void);
typedef void (*KbmThreadFn)(void *arg);

typedef struct KbmThread {
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t thread;
#endif
    KbmThreadFn fn;
    void *arg;
    bool started;
} KbmThread;

bool kbm_thread_start(KbmThread *t, KbmThreadFn fn, void *arg);
bool kbm_thread_join(KbmThread *t, unsigned timeout_ms);
void kbm_sleep_ms(unsigned ms);
void *kbm_find_symbol(const char *name);
void *kbm_find_symbol_in_lib(const char *lib, const char *name);
bool kbm_readable(const void *p, size_t n);
const char *kbm_platform_name(void);
