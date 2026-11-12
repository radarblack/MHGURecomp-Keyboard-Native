#include "boot.h"

#include "config.h"
#include "driver.h"
#include "guest.h"
#include "hooks.h"
#include "itemkeys.h"
#include "host.h"
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

static bool game_ready(void) {
    if (g_host.dispatch_lookup)
        return g_host.dispatch_lookup(KBM_FN_PL_INPUT_UPDATE) != NULL && g_host.dispatch_lookup(KBM_FN_FACE_STICK) != NULL;
    return kbm_guest_signatures_match();
}

static void fail(void) { set_state(KBM_BOOT_FAILED); }

static void worker(void *unused) {
    (void)unused;
    const uint64_t t0 = kbm_now_ms();
    const uint64_t limit_ms = (uint64_t)g_cfg.hook_wait_s * 1000u;

    for (;;) {
        if (stop_requested()) {
            set_state(KBM_BOOT_STOPPED);
            return;
        }
        if (game_ready()) break;
        uint64_t now = kbm_now_ms();
        if (now - t0 >= limit_ms) {
            if (!g_host.dispatch_lookup) kbm_guest_verify();
            fail();
            return;
        }
        kbm_sleep_ms(KBM_POLL_MS);
    }

    if (!kbm_guest_verify()) {
        fail();
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
            fail();
            return;
        }
        kbm_sleep_ms(500);
    }
    kbm_itemkeys_start();
    set_state(KBM_BOOT_ACTIVE);
}

bool kbm_boot_start(void) {
    __atomic_store_n(&g_stop, 0, __ATOMIC_RELEASE);
    set_state(KBM_BOOT_WAITING);
    if (!kbm_thread_start(&g_thread, worker, NULL)) {
        set_state(KBM_BOOT_FAILED);
        return false;
    }
    return true;
}

void kbm_boot_stop(void) {
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    kbm_thread_join(&g_thread, KBM_JOIN_MS);
}
