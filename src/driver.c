#include "driver.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "guest.h"
#include "host.h"
#include "itemkeys.h"
#include "keys.h"
#include "movement.h"
#include "platform.h"

#define MAX_SEEN_ACTS 64
#define FIRST_SNAPS_AT_INFO 20
#define FIRST_REVERSALS_AT_INFO 10

typedef struct SeenAct {
    uint16_t st;
    uint16_t no;
    uint8_t phase;
} SeenAct;

static KbmStats g_stats;
static KbmTracker g_tracker;
static bool g_driving;
static bool g_last_active;
static uint16_t g_last_angle;
static const char *g_suspended_by;
static int g_learned[3];
static unsigned g_last_pack = ~0u;
static SeenAct g_seen[MAX_SEEN_ACTS];
static int g_n_seen;
static bool g_logged_default_mag[3];
static uint64_t g_last_stats_ms;

void kbm_driver_reset(void) {
    memset(&g_stats, 0, sizeof g_stats);
    kbm_tracker_init(&g_tracker);
    g_driving = false;
    g_last_active = false;
    g_last_angle = 0;
    g_suspended_by = NULL;
    memset(g_learned, 0, sizeof g_learned);
    g_last_pack = ~0u;
    g_n_seen = 0;
    memset(g_logged_default_mag, 0, sizeof g_logged_default_mag);
    g_last_stats_ms = kbm_now_ms();
}

const KbmStats *kbm_driver_stats(void) { return &g_stats; }
int kbm_driver_learned(int category) { return category >= 0 && category < 3 ? g_learned[category] : 0; }

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

void kbm_driver_log_stats(KbmLogLevel level, const char *why) {
    const KbmStats *s = &g_stats;
    kbm_itemkeys_log_stats(level, why);
    KBM_LOG(level, "stats (%s): input calls=%llu not_local=%llu suspended=%llu idle=%llu driven=%llu injected=%llu reversals=%llu",
        why, (unsigned long long)s->input_calls, (unsigned long long)s->input_not_local, (unsigned long long)s->input_suspended,
        (unsigned long long)s->input_idle, (unsigned long long)s->input_driven, (unsigned long long)s->injected,
        (unsigned long long)s->reversals);
    KBM_LOG(level, "stats (%s): magnitude game=%llu learned=%llu default=%llu | learned walk=%d run=%d", why,
        (unsigned long long)s->mag_from_game, (unsigned long long)s->mag_from_learned, (unsigned long long)s->mag_from_default,
        g_learned[1], g_learned[2]);
    KBM_LOG(level,
        "stats (%s): face calls=%llu not_driving=%llu not_local=%llu skipped_scope=%llu skipped_act=%llu no_change=%llu turns=%llu partial=%llu reversals=%llu",
        why, (unsigned long long)s->face_calls, (unsigned long long)s->face_not_driving, (unsigned long long)s->face_not_local,
        (unsigned long long)s->face_skipped_scope, (unsigned long long)s->face_skipped_act, (unsigned long long)s->face_no_change,
        (unsigned long long)s->snaps, (unsigned long long)s->turn_partial, (unsigned long long)s->snap_reversals);
}

static void maybe_stats(void) {
    if (!g_cfg.stats_interval_s) return;
    uint64_t now = kbm_now_ms();
    if (now - g_last_stats_ms < (uint64_t)g_cfg.stats_interval_s * 1000u) return;
    g_last_stats_ms = now;
    kbm_driver_log_stats(KBM_LOG_DEBUG, "periodic");
}

static void note_gate(const char *reason) {
    if (reason == g_suspended_by) return;
    if (reason) KBM_LOG(KBM_LOG_INFO, "direct movement suspended: %s is open", reason);
    else KBM_LOG(KBM_LOG_INFO, "direct movement resumed (was suspended by %s)", g_suspended_by);
    g_suspended_by = reason;
}

static int choose_magnitude(const KbmMove *m, int game_mag, const char **source) {
    int target = m->walk ? 1 : 2;
    if (kbm_tilt_category(game_mag) == target) {
        if (!m->contested && g_learned[target] != game_mag) {
            if (!g_learned[target])
                KBM_LOG(KBM_LOG_INFO, "learned the game's %s stick magnitude: %d", target == 1 ? "walk" : "run", game_mag);
            g_learned[target] = game_mag;
        }
        g_stats.mag_from_game++;
        *source = "game";
        return game_mag;
    }
    if (g_learned[target]) {
        g_stats.mag_from_learned++;
        *source = "learned";
        return g_learned[target];
    }
    int def = target == 1 ? KBM_DEFAULT_MAG_WALK : KBM_DEFAULT_MAG_RUN;
    if (!g_logged_default_mag[target]) {
        g_logged_default_mag[target] = true;
        KBM_LOG(KBM_LOG_INFO, "no observed %s magnitude yet, using the default %d until the game shows one",
            target == 1 ? "walk" : "run", def);
    }
    g_stats.mag_from_default++;
    *source = "default";
    return def;
}

void kbm_driver_before_input(KbmCpu *c, uint32_t pl, bool alt) {
    if (!alt) kbm_itemkeys_before_input(c, pl);
}

void kbm_driver_on_input(KbmCpu *c, uint32_t pl, bool alt) {
    g_stats.input_calls++;
    if (!is_local(pl)) {
        g_stats.input_not_local++;
        return;
    }
    maybe_stats();

    const char *gate = gate_reason();
    note_gate(gate);
    if (!alt) kbm_itemkeys_after_input(c, pl, gate);
    if (gate) {
        g_stats.input_suspended++;
        g_driving = false;
        g_last_active = false;
        return;
    }

    KbmKeys keys;
    kbm_keys_sample(&keys);
    unsigned pack = (unsigned)keys.up | (unsigned)keys.down << 1 | (unsigned)keys.left << 2 | (unsigned)keys.right << 3 |
                    (unsigned)keys.walk << 4;
    if (pack != g_last_pack) {
        KBM_LOG(KBM_LOG_DEBUG, "keys: up=%d down=%d left=%d right=%d walk=%d", keys.up, keys.down, keys.left, keys.right, keys.walk);
        g_last_pack = pack;
    }

    KbmMove m = kbm_resolve(&g_tracker, &keys, g_cfg.socd);
    if (!m.active) {
        if (g_last_active) KBM_LOG(KBM_LOG_DEBUG, "movement keys released: the game's own stick state is left alone");
        g_driving = false;
        g_last_active = false;
        g_stats.input_idle++;
        return;
    }

    if (!g_last_active) {
        KBM_LOG(KBM_LOG_DEBUG, "movement started: angle=%04x walk=%d contested=%d", m.angle, m.walk, m.contested);
    } else if (kbm_angle_reversal(g_last_angle, m.angle)) {
        g_stats.reversals++;
        KbmLogLevel lv = g_stats.reversals <= FIRST_REVERSALS_AT_INFO ? KBM_LOG_INFO : KBM_LOG_DEBUG;
        KBM_LOG(lv, "direction reversal %04x -> %04x (delta %04x) contested=%d", g_last_angle, m.angle,
            kbm_angle_delta(g_last_angle, m.angle), m.contested);
    }
    g_driving = true;
    g_last_active = true;
    g_last_angle = m.angle;
    g_stats.input_driven++;

    if (g_cfg.mode == KBM_MODE_PASSTHROUGH) {
        KBM_LOG(KBM_LOG_TRACE, "passthrough: the port's stick state is kept, angle would be %04x", m.angle);
        return;
    }

    uint16_t angle = m.angle;
    if ((int32_t)kbm_ld32(pl + KBM_PL_REVERSED_WORD) > 0) angle = (uint16_t)(angle - 0x8000);
    int game_mag = (int16_t)kbm_ld16(pl + KBM_PL_STICK_MAG);
    const char *source = "";
    int mag = choose_magnitude(&m, game_mag, &source);
    uint16_t old_angle = kbm_ld16(pl + KBM_PL_STICK_ANGLE);
    kbm_st16(pl + KBM_PL_STICK_ANGLE, angle);
    kbm_st16(pl + KBM_PL_STICK_MAG, (uint16_t)mag);
    g_stats.injected++;
    KBM_LOG(KBM_LOG_TRACE, "inject: player %08x angle %04x -> %04x, magnitude %d -> %d (%s)", pl, old_angle, angle, game_mag, mag,
        source);
}

static void note_act(uint16_t st, uint16_t no, unsigned phase, uint16_t before, uint16_t target) {
    for (int i = 0; i < g_n_seen; i++)
        if (g_seen[i].st == st && g_seen[i].no == no && g_seen[i].phase == phase) return;
    if (g_n_seen < MAX_SEEN_ACTS) {
        g_seen[g_n_seen].st = st;
        g_seen[g_n_seen].no = no;
        g_seen[g_n_seen].phase = (uint8_t)phase;
        g_n_seen++;
    }
    KBM_LOG(KBM_LOG_INFO, "face-stick first seen in action %04x:%03u phase %u (yaw %04x, target %04x)", st, no, phase, before, target);
}

void kbm_driver_on_face(uint32_t pl) {
    g_stats.face_calls++;
    if (!g_cfg.snap_facing || g_cfg.turn_mode == KBM_TURN_GAME) return;
    unsigned phase = (kbm_ld32(pl + KBM_PL_PHASE_WORD) & 0xffffu) >> 8;
    uint16_t st = kbm_ld16(pl + KBM_PL_ACT_ST);
    uint16_t no = kbm_ld16(pl + KBM_PL_ACT_NO);
    uint16_t actual = (uint16_t)kbm_ld32(pl + KBM_PL_YAW_ACTUAL);
    uint16_t target = (uint16_t)(kbm_ld32(pl + KBM_PL_YAW_TARGET) & 0xffffu);
    note_act(st, no, phase, actual, target);
    if (!g_driving) {
        g_stats.face_not_driving++;
        return;
    }
    if (!is_local(pl)) {
        g_stats.face_not_local++;
        return;
    }

    if (g_cfg.snap_scope == KBM_SCOPE_UPDATE && phase < 1) {
        g_stats.face_skipped_scope++;
        return;
    }
    if ((g_cfg.n_allow && !kbm_act_listed(g_cfg.snap_allow, g_cfg.n_allow, st, no)) ||
        kbm_act_listed(g_cfg.snap_deny, g_cfg.n_deny, st, no)) {
        g_stats.face_skipped_act++;
        return;
    }
    if (actual == target) {
        g_stats.face_no_change++;
        return;
    }
    uint16_t delta = kbm_angle_delta(actual, target);
    uint16_t next = g_cfg.turn_mode == KBM_TURN_SNAP ? target : kbm_turn_step(actual, target, &g_cfg.turn);
    kbm_st32(pl + KBM_PL_YAW_ACTUAL, next);
    g_stats.snaps++;
    if (next != target) g_stats.turn_partial++;
    if (delta >= 0x5000) g_stats.snap_reversals++;
    KbmLogLevel lv = g_stats.snaps <= FIRST_SNAPS_AT_INFO ? KBM_LOG_INFO : KBM_LOG_TRACE;
    KBM_LOG(lv, "turn yaw %04x -> %04x (target %04x, delta %04x, %s) action %04x:%03u phase %u", actual, next, target, delta,
        next == target ? "reached" : "partial", st, no, phase);
}
