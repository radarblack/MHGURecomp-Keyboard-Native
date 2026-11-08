#include "boot.h"

#include "config.h"
#include "driver.h"
#include "guest.h"
#include "hooks.h"
#include "host.h"
#include "log.h"
#include "platform.h"

#ifndef KBM_POLL_MS
#define KBM_POLL_MS 100
#endif
#ifndef KBM_NOTE_MS
#define KBM_NOTE_MS 10000
#endif
#define KBM_JOIN_MS 5000
#define KBM_INSTALL_TRIES 3

static KbmThread g_thread;
static volatile int g_state = KBM_BOOT_IDLE;
static volatile int g_stop;

KbmBootState kbm_boot_state(void) { return (KbmBootState)__atomic_load_n(&g_state, __ATOMIC_ACQUIRE); }
static void set_state(KbmBootState s) { __atomic_store_n(&g_state, (int)s, __ATOMIC_RELEASE); }
static bool stop_requested(void) { return __atomic_load_n(&g_stop, __ATOMIC_ACQUIRE) != 0; }

const char *kbm_boot_state_name(KbmBootState state) {
    switch (state) {
    case KBM_BOOT_IDLE: return "idle";
    case KBM_BOOT_WAITING: return "waiting for the game";
    case KBM_BOOT_ACTIVE: return "active";
    case KBM_BOOT_FAILED: return "inactive after a failure";
    case KBM_BOOT_STOPPED: return "stopped";
    }
    return "?";
}

static bool game_ready(void) {
    if (g_host.dispatch_lookup)
        return g_host.dispatch_lookup(KBM_FN_PL_INPUT_UPDATE) != NULL && g_host.dispatch_lookup(KBM_FN_FACE_STICK) != NULL;
    return kbm_guest_signatures_match();
}

static void fail(const char *why) {
    KBM_LOG(KBM_LOG_ERROR, "module is inactive: %s", why);
    set_state(KBM_BOOT_FAILED);
}

static void worker(void *unused) {
    (void)unused;
    const uint64_t t0 = kbm_now_ms();
    const uint64_t limit_ms = (uint64_t)g_cfg.hook_wait_s * 1000u;
    uint64_t last_note = t0;
    unsigned polls = 0;

    KBM_LOG(KBM_LOG_INFO, "waiting for the game to load (the port starts engine modules before the game is in memory), %s, up to %us",
        g_host.dispatch_lookup ? "watching dispatch_lookup" : "polling the game's memory", (unsigned)g_cfg.hook_wait_s);

    for (;;) {
        if (stop_requested()) {
            KBM_LOG(KBM_LOG_INFO, "stop requested while waiting: the game never became ready");
            set_state(KBM_BOOT_STOPPED);
            return;
        }
        polls++;
        if (game_ready()) break;
        uint64_t now = kbm_now_ms();
        if (now - t0 >= limit_ms) {
            if (!g_host.dispatch_lookup) kbm_guest_verify();
            fail("the game did not become ready in time (raise HookWaitSeconds, or check that the game starts at all)");
            return;
        }
        if (now - last_note >= KBM_NOTE_MS) {
            last_note = now;
            KBM_LOG(KBM_LOG_INFO, "still waiting for the game to load (%llu s so far)", (unsigned long long)((now - t0) / 1000u));
        }
        kbm_sleep_ms(KBM_POLL_MS);
    }
    KBM_LOG(KBM_LOG_INFO, "the game is loaded: %llu ms after the module started, %u polls", (unsigned long long)(kbm_now_ms() - t0),
        polls);

    if (!kbm_guest_verify()) {
        fail("the game code does not match what the module was written for (see the lines above)");
        return;
    }
    if (stop_requested()) {
        set_state(KBM_BOOT_STOPPED);
        return;
    }

    kbm_driver_reset();
    for (int attempt = 1;; attempt++) {
        if (kbm_hooks_install()) break;
        if (attempt >= KBM_INSTALL_TRIES || stop_requested()) {
            fail("a hook could not be installed (see the lines above)");
            return;
        }
        KBM_LOG(KBM_LOG_WARN, "hook install attempt %d of %d failed, trying again shortly", attempt, KBM_INSTALL_TRIES);
        kbm_sleep_ms(500);
    }
    set_state(KBM_BOOT_ACTIVE);
    KBM_LOG(KBM_LOG_INFO, "ready: %d hook(s) installed, mode=%s, %llu ms after the module started", kbm_hooks_installed_count(),
        g_cfg.mode == KBM_MODE_DIRECT ? "direct" : "passthrough", (unsigned long long)(kbm_now_ms() - t0));
}

bool kbm_boot_start(void) {
    __atomic_store_n(&g_stop, 0, __ATOMIC_RELEASE);
    set_state(KBM_BOOT_WAITING);
    if (!kbm_thread_start(&g_thread, worker, NULL)) {
        set_state(KBM_BOOT_FAILED);
        KBM_LOG(KBM_LOG_ERROR, "could not start the thread that waits for the game");
        return false;
    }
    return true;
}

void kbm_boot_stop(void) {
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    if (!kbm_thread_join(&g_thread, KBM_JOIN_MS))
        KBM_LOG(KBM_LOG_ERROR, "the thread that waits for the game did not stop within %d ms", KBM_JOIN_MS);
}
