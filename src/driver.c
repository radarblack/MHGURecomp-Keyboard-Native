#include "driver.h"

#include <string.h>

#include "config.h"
#include "guest.h"
#include "host.h"
#include "itemkeys.h"
#include "keys.h"
#include "movement.h"
#include "platform.h"

static KbmTracker g_tracker;
static bool g_driving;
static bool g_last_active;
static int g_learned[3];

void kbm_driver_reset(void) {
    kbm_tracker_init(&g_tracker);
    g_driving = false;
    g_last_active = false;
    memset(g_learned, 0, sizeof g_learned);
}

static bool is_local(uint32_t pl) { return (kbm_ld32(pl + KBM_PL_IS_LOCAL_WORD) & 1u) == 1u; }

static const char *gate_reason(void) {
    if (g_host.textinput_active && g_host.textinput_active()) return "text input";
    if (g_host.chat_active && g_host.chat_active()) return "chat";
    if (g_host.overlay_menu_open) {
        if (g_host.overlay_menu_open()) return "menu overlay";
    } else if (g_host.sdl_keyboard_focus && g_host.sdl_text_input_active) {
        void *window = g_host.sdl_keyboard_focus();
        if (window && g_host.sdl_text_input_active(window)) return "menu or text entry";
    }
    return NULL;
}

static int choose_magnitude(const KbmMove *m, int game_mag) {
    int target = m->walk ? 1 : 2;
    if (kbm_tilt_category(game_mag) == target) {
        if (!m->contested && g_learned[target] != game_mag) g_learned[target] = game_mag;
        return game_mag;
    }
    if (g_learned[target]) return g_learned[target];
    return target == 1 ? KBM_DEFAULT_MAG_WALK : KBM_DEFAULT_MAG_RUN;
}

typedef enum LockLevel { LOCK_NONE = 0, LOCK_BUTTONS, LOCK_ALL } LockLevel;

static bool sane_addr(uint32_t a) { return a >= 0x00200000u && a < 0xF0000000u && !(a & 3u); }

static bool read_chain(uint32_t got, uint32_t *obj) {
    uint32_t p = kbm_ld32(got);
    if (!sane_addr(p)) return false;
    uint32_t o = kbm_ld32(p);
    if (!sane_addr(o)) return false;
    *obj = o;
    return true;
}

static LockLevel input_lock(bool alt) {
    uint32_t obj = 0;
    if (!g_cfg.respect_locks) return LOCK_NONE;
    if (alt) {
        if (!read_chain(KBM_GOT_STATE, &obj)) return LOCK_NONE;
        uint32_t state = kbm_ld32(obj + KBM_STATE_WORD);
        uint32_t u = state - 1u;
        if (u <= 0x1au && ((0x4000041u >> u) & 1u)) return LOCK_NONE;
        return LOCK_ALL;
    }
    if (!read_chain(KBM_GOT_MODE, &obj)) return LOCK_NONE;
    unsigned m = (unsigned)(*(const uint8_t *)KBM_GPTR(obj + KBM_MODE_BYTE));
    if (m == 5 || m == 6) return LOCK_ALL;
    if (m == 4 || m == 7 || m == 9) return LOCK_BUTTONS;
    return LOCK_NONE;
}

void kbm_driver_before_input(KbmCpu *c, uint32_t pl, bool alt) {
    if (!alt) kbm_itemkeys_before_input(c, pl);
}

void kbm_driver_on_input(KbmCpu *c, uint32_t pl, bool alt) {
    if (!is_local(pl)) return;

    const char *gate = gate_reason();
    LockLevel lock = input_lock(alt);
    if (!alt) kbm_itemkeys_after_input(c, pl, gate ? gate : (lock != LOCK_NONE ? "game input lock" : NULL));
    if (lock == LOCK_ALL && !gate) {
        g_driving = false;
        g_last_active = false;
        return;
    }
    if (gate) {
        g_driving = false;
        g_last_active = false;
        return;
    }

    KbmKeys keys;
    kbm_keys_sample(&keys);

    KbmMove m = kbm_resolve(&g_tracker, &keys, g_cfg.socd);
    if (!m.active) {
        g_driving = false;
        g_last_active = false;
        return;
    }

    g_driving = true;
    g_last_active = true;

    if (g_cfg.mode == KBM_MODE_PASSTHROUGH) return;

    uint16_t angle = m.angle;
    int32_t reversed_word = (int32_t)kbm_ld32(pl + KBM_PL_REVERSED_WORD);
    if (!alt && reversed_word > 0) angle = (uint16_t)(angle - 0x8000);
    int game_mag = (int16_t)kbm_ld16(pl + KBM_PL_STICK_MAG);
    int mag = choose_magnitude(&m, game_mag);
    kbm_st16(pl + KBM_PL_STICK_ANGLE, angle);
    kbm_st16(pl + KBM_PL_STICK_MAG, (uint16_t)mag);
}

void kbm_driver_on_face(uint32_t pl) {
    if (!g_cfg.snap_facing || g_cfg.turn_mode == KBM_TURN_GAME) return;
    unsigned phase = (kbm_ld32(pl + KBM_PL_PHASE_WORD) & 0xffffu) >> 8;
    uint16_t st = kbm_ld16(pl + KBM_PL_ACT_ST);
    uint16_t no = kbm_ld16(pl + KBM_PL_ACT_NO);
    uint16_t actual = (uint16_t)kbm_ld32(pl + KBM_PL_YAW_ACTUAL);
    uint16_t target = (uint16_t)(kbm_ld32(pl + KBM_PL_YAW_TARGET) & 0xffffu);
    if (!g_driving) return;
    if (!is_local(pl)) return;

    if (g_cfg.snap_scope == KBM_SCOPE_UPDATE && phase < 1) return;
    if ((g_cfg.n_allow && !kbm_act_listed(g_cfg.snap_allow, g_cfg.n_allow, st, no)) ||
        kbm_act_listed(g_cfg.snap_deny, g_cfg.n_deny, st, no))
        return;
    if (actual == target) return;
    uint16_t next = g_cfg.turn_mode == KBM_TURN_SNAP ? target : kbm_turn_step(actual, target, &g_cfg.turn);
    kbm_st32(pl + KBM_PL_YAW_ACTUAL, next);
}
