#include "platform.h"

#ifdef _WIN32

void kbm_mutex_init(KbmMutex *m) { InitializeCriticalSection(m); }
void kbm_mutex_lock(KbmMutex *m) { EnterCriticalSection(m); }
void kbm_mutex_unlock(KbmMutex *m) { LeaveCriticalSection(m); }
uint64_t kbm_now_ms(void) { return (uint64_t)GetTickCount64(); }
uint32_t kbm_thread_id(void) { return (uint32_t)GetCurrentThreadId(); }


static DWORD WINAPI thread_main(LPVOID p) {
    KbmThread *t = (KbmThread *)p;
    t->fn(t->arg);
    return 0;
}

bool kbm_thread_start(KbmThread *t, KbmThreadFn fn, void *arg) {
    t->fn = fn;
    t->arg = arg;
    t->handle = CreateThread(NULL, 0, thread_main, t, 0, NULL);
    t->started = t->handle != NULL;
    return t->started;
}

bool kbm_thread_join(KbmThread *t, unsigned timeout_ms) {
    if (!t->started) return true;
    if (WaitForSingleObject(t->handle, timeout_ms) != WAIT_OBJECT_0) return false;
    CloseHandle(t->handle);
    t->started = false;
    return true;
}

void kbm_sleep_ms(unsigned ms) { Sleep(ms); }

void *kbm_find_symbol(const char *name) {
    HMODULE exe = GetModuleHandleA(NULL);
    return exe ? (void *)GetProcAddress(exe, name) : NULL;
}

void *kbm_find_symbol_in_lib(const char *lib, const char *name) {
    HMODULE m = lib ? GetModuleHandleA(lib) : NULL;
    return m ? (void *)GetProcAddress(m, name) : NULL;
}

bool kbm_readable(const void *p, size_t n) {
    const unsigned char *a = (const unsigned char *)p;
    size_t done = 0;
    while (done < n) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(a + done, &mbi, sizeof mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                PAGE_EXECUTE_WRITECOPY)))
            return false;
        size_t region_end = (size_t)((const unsigned char *)mbi.BaseAddress + mbi.RegionSize - a);
        if (region_end <= done) return false;
        done = region_end;
    }
    return true;
}

const char *kbm_platform_name(void) { return "windows"; }

#else

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

void kbm_mutex_init(KbmMutex *m) { pthread_mutex_init(m, NULL); }
void kbm_mutex_lock(KbmMutex *m) { pthread_mutex_lock(m); }
void kbm_mutex_unlock(KbmMutex *m) { pthread_mutex_unlock(m); }

uint64_t kbm_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint32_t kbm_thread_id(void) { return (uint32_t)syscall(SYS_gettid); }


static void *thread_main(void *p) {
    KbmThread *t = (KbmThread *)p;
    t->fn(t->arg);
    return NULL;
}

bool kbm_thread_start(KbmThread *t, KbmThreadFn fn, void *arg) {
    t->fn = fn;
    t->arg = arg;
    t->started = pthread_create(&t->thread, NULL, thread_main, t) == 0;
    return t->started;
}

bool kbm_thread_join(KbmThread *t, unsigned timeout_ms) {
    if (!t->started) return true;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    if (pthread_timedjoin_np(t->thread, NULL, &ts) != 0) return false;
    t->started = false;
    return true;
}

void kbm_sleep_ms(unsigned ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

void *kbm_find_symbol(const char *name) { return dlsym(RTLD_DEFAULT, name); }

void *kbm_find_symbol_in_lib(const char *lib, const char *name) {
    void *h = lib ? dlopen(lib, RTLD_LAZY | RTLD_NOLOAD) : NULL;
    return h ? dlsym(h, name) : NULL;
}

bool kbm_readable(const void *p, size_t n) {
    static int fds[2] = {-1, -1};
    if (fds[0] < 0) {
        if (pipe(fds)) return false;
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        fcntl(fds[1], F_SETFL, O_NONBLOCK);
    }
    const unsigned char *a = (const unsigned char *)p;
    char sink[256];
    while (n) {
        size_t chunk = n < sizeof sink ? n : sizeof sink;
        ssize_t w = write(fds[1], a, chunk);
        if (w <= 0) return false;
        ssize_t r = read(fds[0], sink, (size_t)w);
        if (r != w) return false;
        a += w;
        n -= (size_t)w;
    }
    return true;
}

const char *kbm_platform_name(void) { return "linux"; }

#endif
