#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "boot.h"
#include "check.h"
#include "engine_module_api.h"
#include "guest.h"
#include "host.h"
#include "driver.h"
#include "itemkeys.h"
#include "platform.h"

#define PLAYER 0x08000000u
#define HLE_START 0xf0000000u
#define MAX_HLE 16
#define MAX_HOOK 16
#define POUCH_BASE 0x07e00000u
#define VTABLE 0x07f00000u
#define FAKE_CAN_SELECT 0x00300000u
#define GOT_P 0x07d00000u
#define SPAD 0x07c00000u
#define STATE_P 0x07b00000u
#define STATE_OBJ 0x07b10000u
#define MODE_P 0x07b20000u
#define MODE_OBJ 0x07b30000u
#define MOCK_EXPORT __attribute__((visibility("default")))

MHGU_ENGINE_MODULE_LINK bool mhgu_engine_module_entry(const MhguHost *host, MhguEngineModule *out);

static struct {
    const char *name;
    KbmHostFn fn;
} g_hle[MAX_HLE];
static int g_nhle;

static struct {
    uint32_t addr, detour;
} g_hook[MAX_HOOK];
static int g_nhook;

static bool g_keys[512];
static bool g_chat, g_typing, g_menu;
static int g_port_angle, g_port_mag;
static uint32_t g_camera_yaw;
static int g_other_calls;
static bool g_dispatch_ready;
static int g_busy;
static int g_can_select;

MOCK_EXPORT uint32_t hle_register(const char *name, KbmHostFn fn) {
    for (int i = 0; i < g_nhle; i++)
        if (!strcmp(g_hle[i].name, name)) return HLE_START + 16u * (uint32_t)i;
    g_hle[g_nhle].name = strdup(name);
    g_hle[g_nhle].fn = fn;
    return HLE_START + 16u * (uint32_t)g_nhle++;
}

MOCK_EXPORT bool fnhook_set(uint32_t addr, uint32_t detour) {
    if (addr < 0x200000u || addr >= 0x2000000u) return false;
    for (int i = 0; i < g_nhook; i++) {
        if (g_hook[i].addr == addr) {
            g_hook[i].detour = detour;
            return true;
        }
    }
    g_hook[g_nhook].addr = addr;
    g_hook[g_nhook].detour = detour;
    g_nhook++;
    return true;
}

MOCK_EXPORT uint32_t fnhook_get(uint32_t addr) {
    for (int i = 0; i < g_nhook; i++)
        if (g_hook[i].addr == addr) return g_hook[i].detour;
    return 0;
}

MOCK_EXPORT void fnhook_call_original(KbmCpu *c, uint32_t addr) {
    uint32_t pl = c->r[0];
    if (addr == KBM_FN_PL_INPUT_UPDATE || addr == KBM_FN_PL_INPUT_UPDATE_ALT) {
        kbm_st16(pl + KBM_PL_STICK_ANGLE, (uint16_t)g_port_angle);
        kbm_st16(pl + KBM_PL_STICK_MAG, (uint16_t)g_port_mag);
    } else if (addr == KBM_FN_FACE_STICK) {
        uint16_t angle = kbm_ld16(pl + KBM_PL_STICK_ANGLE);
        kbm_st32(pl + KBM_PL_YAW_TARGET, (g_camera_yaw + angle + 0xc000u) & 0xffffu);
    }
}

MOCK_EXPORT void guest_call(KbmCpu *c, uint32_t addr) {
    if (addr == KBM_FN_PL_BUSY) {
        c->r[0] = (uint32_t)g_busy;
        return;
    }
    if (addr == KBM_FN_POUCH_ENTRY) {
        c->r[0] = c->r[1] < 0x32 ? POUCH_BASE + 0x1au + 4u * c->r[1] : 0;
        return;
    }
    if (addr == KBM_FN_ITEM_USABLE) {
        c->r[0] = c->r[1] == 1 || c->r[1] == 2 || c->r[1] == 3;
        return;
    }
    if (addr == FAKE_CAN_SELECT) {
        c->r[0] = (uint32_t)g_can_select;
        return;
    }
    if (addr >= HLE_START && addr < HLE_START + 16u * MAX_HLE) {
        KbmHostFn fn = g_hle[(addr - HLE_START) / 16].fn;
        if (fn) fn(c);
    }
}

#ifndef MOCK_NO_DISPATCH
MOCK_EXPORT void *dispatch_lookup(uint32_t addr) {
    (void)addr;
    return g_dispatch_ready ? (void *)(uintptr_t)1 : NULL;
}
#endif
MOCK_EXPORT bool textinput_active(void) { return g_typing; }
MOCK_EXPORT bool chat_active(void) { return g_chat; }
#ifdef MOCK_OVERLAY_EXPORT
MOCK_EXPORT bool overlay_menu_open(void) { return g_menu; }
#endif
#ifndef MOCK_NO_MENU_GATE
MOCK_EXPORT void *SDL_GetKeyboardFocus(void) { return (void *)(uintptr_t)1; }
MOCK_EXPORT bool SDL_TextInputActive(void *window) {
    (void)window;
#ifdef MOCK_OVERLAY_EXPORT
    return false;
#else
    return g_menu;
#endif
}
#endif
MOCK_EXPORT const bool *SDL_GetKeyboardState(int *n) {
    if (n) *n = 512;
    return g_keys;
}
MOCK_EXPORT int SDL_GetScancodeFromName(const char *name) {
    static const struct {
        const char *n;
        int c;
    } t[] = {{"W", 26}, {"S", 22}, {"A", 4}, {"D", 7}, {"Left Shift", 225}, {"Up", 82}, {"1", 30}, {"2", 31}, {"3", 32}, {"4", 33}, {"5", 34}, {"6", 35}, {"7", 36}, {"8", 37}, {"9", 38}, {"0", 39}, {"-", 45}, {"=", 46}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcasecmp(name, t[i].n)) return t[i].c;
    return 0;
}
MOCK_EXPORT uint32_t SDL_GetMouseState(float *x, float *y) {
    (void)x;
    (void)y;
    return 0;
}

enum { K_W = 26, K_S = 22, K_A = 4, K_D = 7, K_SHIFT = 225, K_1 = 30, K_2 = 31, K_3 = 32 };

static void host_log(const char *fmt, ...) { (void)fmt; }

static bool host_setting(const char *key, char *out, size_t n) {
    static const struct {
        const char *k, *v;
    } t[] = {{"MoveUp", "W"}, {"MoveDown", "S"}, {"MoveLeft", "A"}, {"MoveRight", "D"}, {"Walk", "Left Shift"}, {"ItemRight", "5"}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        if (!strcmp(key, t[i].k)) {
            snprintf(out, n, "%s", t[i].v);
            return true;
        }
    }
    return false;
}

static char g_dir[256];
static MhguHost g_host_tbl;
static MhguEngineModule g_mod;

static void put_words(uint32_t addr, const uint32_t *w) {
    for (int i = 0; i < KBM_SIG_WORDS; i++) kbm_st32(addr + 4u * (uint32_t)i, w[i]);
}

static void reset_world(void) {
    g_nhook = 0;
    memset(g_keys, 0, sizeof g_keys);
    g_chat = g_typing = g_menu = false;
    g_port_angle = 0;
    g_port_mag = 0;
    g_camera_yaw = 0x1000;
    g_other_calls = 0;
    g_dispatch_ready = true;
    for (size_t i = 0; i < KBM_GUEST_SIG_COUNT; i++) put_words(kbm_guest_sigs[i].addr, kbm_guest_sigs[i].words);
    kbm_st32(PLAYER + KBM_PL_IS_LOCAL_WORD, 1);
    kbm_st32(PLAYER + KBM_PL_REVERSED_WORD, 0);
    kbm_st32(PLAYER + KBM_PL_PHASE_WORD, 0x0100);
    kbm_st16(PLAYER + KBM_PL_ACT_ST, 0x1000);
    kbm_st16(PLAYER + KBM_PL_ACT_NO, 1001);
    kbm_st32(PLAYER + KBM_PL_YAW_ACTUAL, 0x1234);
    kbm_st32(PLAYER + KBM_PL_YAW_TARGET, 0x1234);
    kbm_st16(PLAYER + KBM_PL_STICK_ANGLE, 0);
    kbm_st16(PLAYER + KBM_PL_STICK_MAG, 0);
    g_busy = 0;
    g_can_select = 1;
    kbm_st32(PLAYER, VTABLE);
    kbm_st32(VTABLE + KBM_VF_CAN_SELECT, FAKE_CAN_SELECT);
    kbm_st16(PLAYER + KBM_PL_ITEM_SLOT, KBM_ITEM_NO_ITEM);
    kbm_st32(PLAYER + KBM_PL_ITEM_FLAGS, 0);
    for (int slot = 0; slot < 0x32; slot++) {
        kbm_st16(POUCH_BASE + 0x1au + 4u * (uint32_t)slot, 0);
        kbm_st16(POUCH_BASE + 0x1cu + 4u * (uint32_t)slot, 0);
    }
    const uint16_t items[][2] = {{1, 5}, {2, 3}, {0x100, 4}, {0, 0}, {3, 0}, {1, 9}};
    for (int slot = 0; slot < 6; slot++) {
        kbm_st16(POUCH_BASE + 0x1au + 4u * (uint32_t)slot, items[slot][0]);
        kbm_st16(POUCH_BASE + 0x1cu + 4u * (uint32_t)slot, items[slot][1]);
    }
    kbm_st32(KBM_GOT_SPAD, GOT_P);
    kbm_st32(GOT_P, SPAD);
    kbm_st32(SPAD + KBM_SPAD_HELD, 0);
    kbm_st32(SPAD + KBM_SPAD_LAST, 0);
    kbm_st32(SPAD + KBM_SPAD_NEW, 0);
    kbm_st32(KBM_GOT_STATE, STATE_P);
    kbm_st32(STATE_P, STATE_OBJ);
    kbm_st32(STATE_OBJ + KBM_STATE_WORD, 1);
    kbm_st32(KBM_GOT_MODE, MODE_P);
    kbm_st32(MODE_P, MODE_OBJ);
    *(uint8_t *)KBM_GPTR(MODE_OBJ + KBM_MODE_BYTE) = 0;
}

static void write_ini(const char *text) {
    char path[512];
    snprintf(path, sizeof path, "%s/keyboard_movement.ini", g_dir);
    if (!text) {
        remove(path);
        return;
    }
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    if (!strstr(text, "TurnMode")) fputs("\n[Movement]\nTurnMode = snap\n", f);
    if (!strstr(text, "SamplerMs")) fputs("\n[ItemHotkeys]\nSamplerMs = 0\n", f);
    fclose(f);
}

static bool wait_terminal(unsigned timeout_ms) {
    for (unsigned waited = 0; waited < timeout_ms; waited += 5) {
        if (kbm_boot_state() != KBM_BOOT_WAITING) break;
        usleep(5000);
    }
    return kbm_boot_state() == KBM_BOOT_ACTIVE;
}

static bool start_module(void) {
    memset(&g_mod, 0, sizeof g_mod);
    g_host_tbl = (MhguHost){.api = MHGU_ENGINE_MODULE_API, .log = host_log, .setting = host_setting, .module_dir = g_dir,
        .port_version = "0.15.1"};
    return mhgu_engine_module_entry(&g_host_tbl, &g_mod);
}

static bool load_module(void) {
    if (!start_module()) return false;
    if (wait_terminal(3000)) return true;
    if (g_mod.shutdown) g_mod.shutdown();
    return false;
}

static void call_hooked(uint32_t fn_addr) {
    KbmCpu c;
    memset(&c, 0, sizeof c);
    c.r[0] = PLAYER;
    uint32_t d = fnhook_get(fn_addr);
    if (d) guest_call(&c, d);
    else fnhook_call_original(&c, fn_addr);
}

static void frame(void) {
    call_hooked(KBM_FN_PL_INPUT_UPDATE);
    call_hooked(KBM_FN_FACE_STICK);
}

static void frame_alt(void) {
    call_hooked(KBM_FN_PL_INPUT_UPDATE_ALT);
    call_hooked(KBM_FN_FACE_STICK);
}

static uint32_t yaw_actual(void) { return kbm_ld32(PLAYER + KBM_PL_YAW_ACTUAL); }
static uint32_t yaw_target(void) { return kbm_ld32(PLAYER + KBM_PL_YAW_TARGET); }
static int stick_angle(void) { return kbm_ld16(PLAYER + KBM_PL_STICK_ANGLE); }
static int stick_mag(void) { return (int16_t)kbm_ld16(PLAYER + KBM_PL_STICK_MAG); }

static char *read_log(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/keyboard_movement.log", g_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return strdup("");
    static char buf[1 << 18];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void test_clean_flow(void) {
    reset_world();
    write_ini("[General]\nLogLevel = trace\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    CHECK(g_mod.shutdown != NULL);
    CHECK(g_mod.name != NULL);
    CHECK(fnhook_get(KBM_FN_PL_INPUT_UPDATE) != 0);
    CHECK(fnhook_get(KBM_FN_FACE_STICK) != 0);
    CHECK(fnhook_get(KBM_FN_PL_INPUT_UPDATE_ALT) != 0);

    g_port_angle = 0;
    g_port_mag = 0;
    g_keys[K_W] = true;
    g_port_angle = 0x4000;
    g_port_mag = 150;
    frame();
    CHECK_EQ(stick_angle(), 0x4000);
    CHECK_EQ(stick_mag(), 150);
    CHECK_EQ(yaw_target(), (g_camera_yaw + 0x4000u + 0xc000u) & 0xffffu);
    CHECK_EQ(yaw_actual(), yaw_target());

    g_keys[K_S] = true;
    g_port_angle = 0;
    g_port_mag = 0;
    frame();
    CHECK_EQ(stick_angle(), 0xc000);
    CHECK(stick_mag() >= KBM_TILT_RUN_MIN);
    CHECK_EQ(yaw_target(), (g_camera_yaw + 0xc000u + 0xc000u) & 0xffffu);
    CHECK_EQ(yaw_actual(), yaw_target());

    g_keys[K_W] = false;
    g_keys[K_S] = false;
    g_port_angle = 0x1111;
    g_port_mag = 0;
    kbm_st32(PLAYER + KBM_PL_YAW_ACTUAL, 0x2222);
    frame();
    CHECK_EQ(stick_angle(), 0x1111);
    CHECK_EQ(stick_mag(), 0);
    CHECK_EQ(yaw_actual(), 0x2222);

    g_mod.shutdown();
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE_ALT), 0);
    CHECK_EQ(fnhook_get(KBM_FN_FACE_STICK), 0);
    char *log = read_log();
    CHECK(strstr(log, "hook keyboard_movement:player_input: installed"));
    CHECK(strstr(log, "hook keyboard_movement:player_input_alt: installed"));
    CHECK(strstr(log, "readback verified"));
    CHECK(strstr(log, "first call arrived"));
    CHECK(strstr(log, "direction reversal"));
    CHECK(strstr(log, "turn yaw"));
    CHECK(strstr(log, "removed from"));
    CHECK(strstr(log, "all hooks released"));
    CHECK(strstr(log, "shutdown complete"));
}

static void test_walk_and_learning(void) {
    reset_world();
    write_ini("[General]\nLogLevel = debug\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    g_keys[K_W] = true;
    g_keys[K_SHIFT] = true;
    g_port_angle = 0x4000;
    g_port_mag = 77;
    frame();
    CHECK_EQ(stick_mag(), 77);
    g_port_mag = 0;
    frame();
    CHECK_EQ(stick_mag(), 77);
    g_keys[K_SHIFT] = false;
    frame();
    CHECK_EQ(stick_mag(), KBM_DEFAULT_MAG_RUN);
    g_port_mag = 160;
    frame();
    CHECK_EQ(stick_mag(), 160);
    g_port_mag = 0;
    frame();
    CHECK_EQ(stick_mag(), 160);
    g_mod.shutdown();
}

static void test_reversed_controls_and_remote(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    kbm_st32(PLAYER + KBM_PL_REVERSED_WORD, 1);
    g_keys[K_W] = true;
    frame();
    CHECK_EQ(stick_angle(), (0x4000 - 0x8000) & 0xffff);
    kbm_st32(PLAYER + KBM_PL_REVERSED_WORD, 0);
    kbm_st32(PLAYER + KBM_PL_IS_LOCAL_WORD, 0);
    g_port_angle = 0x7777;
    g_port_mag = 5;
    kbm_st32(PLAYER + KBM_PL_YAW_ACTUAL, 0x3333);
    frame();
    CHECK_EQ(stick_angle(), 0x7777);
    CHECK_EQ(yaw_actual(), 0x3333);
    g_mod.shutdown();
}

static void test_gates(void) {
    const char *names[3] = {"typing", "chat", "menu"};
    for (int g = 0; g < 3; g++) {
        reset_world();
        write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
        CHECK(load_module());
        g_keys[K_W] = true;
        g_port_angle = 0x2222;
        g_port_mag = 140;
        g_typing = g == 0;
        g_chat = g == 1;
        g_menu = g == 2;
        kbm_st32(PLAYER + KBM_PL_YAW_ACTUAL, 0x0abc);
        frame();
        CHECK_EQ(stick_angle(), 0x2222);
        CHECK_EQ(yaw_actual(), 0x0abc);
        g_typing = g_chat = g_menu = false;
        frame();
        CHECK_EQ(stick_angle(), 0x4000);
        g_mod.shutdown();
        char *log = read_log();
        CHECK(strstr(log, "direct movement suspended"));
        CHECK(strstr(log, "direct movement resumed"));
        if (g == 2) {
#ifdef MOCK_OVERLAY_EXPORT
            CHECK(strstr(log, "menu overlay is open"));
            CHECK(strstr(log, "menu gate: the port's overlay_menu_open()"));
#else
            CHECK(strstr(log, "menu or text entry is open"));
            CHECK(strstr(log, "overlay_menu_open is not exported by this build"));
#endif
        }
        (void)names;
    }
}

static void test_snap_scope_and_deny(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[Movement]\nSnapScope = update\nSnapDeny = 1000:1001\n");
    CHECK(load_module());
    g_keys[K_W] = true;
    g_port_angle = 0x4000;
    g_port_mag = 150;
    frame();
    CHECK(yaw_actual() != yaw_target());
    g_mod.shutdown();

    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[Movement]\nSnapScope = update\n");
    CHECK(load_module());
    kbm_st32(PLAYER + KBM_PL_PHASE_WORD, 0x0000);
    g_keys[K_W] = true;
    g_port_angle = 0x4000;
    g_port_mag = 150;
    frame();
    CHECK(yaw_actual() != yaw_target());
    kbm_st32(PLAYER + KBM_PL_PHASE_WORD, 0x0100);
    frame();
    CHECK_EQ(yaw_actual(), yaw_target());
    g_mod.shutdown();

    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[Movement]\nSnapFacing = 0\n");
    CHECK(load_module());
    g_keys[K_W] = true;
    g_port_angle = 0x4000;
    g_port_mag = 150;
    frame();
    CHECK(yaw_actual() != yaw_target());
    CHECK_EQ(stick_angle(), 0x4000);
    g_mod.shutdown();
}

static void test_smooth_turn(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[Movement]\nTurnMode = smooth\nTurnResponse = 0.5\n"
              "TurnMinStepDegrees = 10\nTurnMaxStepDegrees = 50\n");
    CHECK(load_module());
    g_keys[K_W] = true;
    g_port_angle = 0x4000;
    g_port_mag = 150;
    uint32_t target = (g_camera_yaw + 0x4000u + 0xc000u) & 0xffffu;
    kbm_st32(PLAYER + KBM_PL_YAW_ACTUAL, (target + 0x8000u) & 0xffffu);
    frame();
    CHECK_EQ(yaw_target(), target);
    CHECK(yaw_actual() != target);
    uint16_t prev = 0xffff;
    int frames = 1;
    while (yaw_actual() != target && frames < 20) {
        uint16_t now = (uint16_t)(((int32_t)(int16_t)(uint16_t)(target - yaw_actual())) < 0 ? -(int16_t)(uint16_t)(target - yaw_actual())
                                                                                                : (int16_t)(uint16_t)(target - yaw_actual()));
        CHECK(now < prev);
        prev = now;
        frame();
        frames++;
    }
    CHECK_EQ(yaw_actual(), target);
    CHECK(frames >= 4);
    CHECK(frames <= 8);

    g_keys[K_S] = true;
    g_port_angle = 0;
    g_port_mag = 0;
    uint32_t back = (g_camera_yaw + 0xc000u + 0xc000u) & 0xffffu;
    frame();
    CHECK_EQ(yaw_target(), back);
    CHECK(yaw_actual() != back);
    for (int i = 0; i < 10; i++) frame();
    CHECK_EQ(yaw_actual(), back);

    g_keys[K_W] = false;
    g_keys[K_S] = false;
    kbm_st32(PLAYER + KBM_PL_YAW_ACTUAL, 0x2222);
    frame();
    CHECK_EQ(yaw_actual(), 0x2222);
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "partial"));
    CHECK(strstr(log, "reached"));
    CHECK(strstr(log, "turn_mode=smooth"));
}

static void test_safe_area_hook(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    kbm_st16(PLAYER + KBM_PL_STICK_ANGLE, 0);
    g_keys[K_D] = true;
    g_port_angle = 0;
    g_port_mag = 150;
    frame_alt();
    CHECK_EQ(stick_angle(), 0x0000);
    g_keys[K_D] = false;
    g_keys[K_W] = true;
    g_port_angle = 0x1111;
    g_port_mag = 0;
    frame_alt();
    CHECK_EQ(stick_angle(), 0x4000);
    CHECK(stick_mag() >= KBM_TILT_WALK_MIN);
    CHECK_EQ(yaw_actual(), yaw_target());
    g_mod.shutdown();
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE_ALT), 0);

    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[Movement]\nSafeAreaHook = 0\n");
    CHECK(load_module());
    CHECK(fnhook_get(KBM_FN_PL_INPUT_UPDATE) != 0);
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE_ALT), 0);
    char *log = read_log();
    CHECK(strstr(log, "turned off by the config"));
    CHECK(strstr(log, "ready: 2 hook(s) installed"));
    g_mod.shutdown();
}

static void test_game_turn_mode(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[Movement]\nTurnMode = game\n");
    CHECK(load_module());
    g_keys[K_W] = true;
    g_port_angle = 0x4000;
    g_port_mag = 150;
    frame();
    CHECK(yaw_actual() != yaw_target());
    CHECK_EQ(stick_angle(), 0x4000);
    g_mod.shutdown();
}

static void press(int key, uint32_t age_ms) { kbm_itemkeys_push(key, (uint32_t)kbm_now_ms() - age_ms); }
static uint32_t spad_held(void) { return kbm_ld32(SPAD + KBM_SPAD_HELD); }
static uint32_t spad_new(void) { return kbm_ld32(SPAD + KBM_SPAD_NEW); }
static int bar_slot(void) { return kbm_ld16(PLAYER + KBM_PL_ITEM_SLOT); }

static void test_item_select(void) {
    reset_world();
    write_ini("[General]\nLogLevel = debug\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    CHECK(kbm_itemkeys_enabled());

    press(0, 50);
    frame();
    CHECK_EQ(bar_slot(), 0);
    CHECK((kbm_ld32(PLAYER + KBM_PL_ITEM_FLAGS) & 6u) != 0);

    press(1, 50);
    frame();
    CHECK_EQ(bar_slot(), 1);

    press(2, 50);
    frame();
    CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);

    press(3, 50);
    frame();
    CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);

    press(1, 50);
    frame();
    CHECK_EQ(bar_slot(), 1);
    press(4, 50);
    frame();
    CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);

    press(5, 50);
    frame();
    CHECK_EQ(bar_slot(), 5);

    press(11, 50);
    frame();
    CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);

    g_busy = 1;
    press(0, 50);
    frame();
    CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);
    g_busy = 0;
    g_can_select = 0;
    press(0, 50);
    frame();
    CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);
    g_can_select = 1;
    press(0, 50);
    frame();
    CHECK_EQ(bar_slot(), 0);

    const KbmItemStats *st = kbm_itemkeys_stats();
    CHECK_EQ(st->blocked_busy, 2);
    CHECK(st->selected_item >= 5);
    CHECK(st->selected_no_item >= 4);
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "bar selected slot 0"));
    CHECK(strstr(log, "the item is not usable on the item bar"));
    CHECK(strstr(log, "the slot is empty"));
    CHECK(strstr(log, "No Item"));
    CHECK(strstr(log, "pouch slot  3: item  256 x4   NOT usable on the bar"));
    CHECK(strstr(log, "ignored, the hunter cannot pick an item right now"));
}

static void test_item_slot_mapping(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[ItemHotkeys]\nItemSlot1 = 2\nItemSlot2 = 1\nSamplerMs = 0\n");
    CHECK(load_module());
    press(0, 50);
    frame();
    CHECK_EQ(bar_slot(), 1);
    press(1, 50);
    frame();
    CHECK_EQ(bar_slot(), 0);
    g_mod.shutdown();
}

static void test_item_double_press(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[ItemHotkeys]\nUsePadMask = 0x800\nUseHoldUpdates = 3\nSamplerMs = 0\n");
    CHECK(load_module());

    press(0, 200);
    press(0, 100);
    frame();
    CHECK_EQ(bar_slot(), 0);
    CHECK_EQ(spad_held(), 0);
    frame();
    CHECK_EQ(spad_held() & 0x800u, 0x800u);
    CHECK_EQ(spad_new() & 0x800u, 0x800u);
    kbm_st32(SPAD + KBM_SPAD_NEW, 0);
    frame();
    CHECK_EQ(spad_held() & 0x800u, 0x800u);
    CHECK_EQ(spad_new() & 0x800u, 0);
    frame();
    CHECK_EQ(kbm_itemkeys_stats()->injected_updates, 3);
    kbm_st32(SPAD + KBM_SPAD_HELD, 0);
    frame();
    CHECK_EQ(spad_held(), 0);
    CHECK_EQ(kbm_itemkeys_stats()->uses, 1);

    press(2, 200);
    press(2, 100);
    frame();
    frame();
    CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);
    CHECK_EQ(spad_held(), 0);
    CHECK_EQ(kbm_itemkeys_stats()->use_nothing, 1);

    press(1, 550);
    press(1, 50);
    frame();
    frame();
    CHECK_EQ(bar_slot(), 1);
    CHECK_EQ(spad_held(), 0);

    press(1, 200);
    press(1, 150);
    press(1, 100);
    frame();
    CHECK_EQ(kbm_itemkeys_stats()->uses, 2);
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "use requested, pad mask 00000800"));
    CHECK(strstr(log, "[double press]"));
    CHECK(strstr(log, "nothing to use in that slot"));
}

static void test_item_use_unconfigured(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    press(0, 200);
    press(0, 100);
    frame();
    frame();
    CHECK_EQ(bar_slot(), 0);
    CHECK_EQ(spad_held(), 0);
    CHECK_EQ(kbm_itemkeys_stats()->use_unconfigured, 1);
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "UsePadMask is not set"));
    CHECK(strstr(log, "ProbeInput"));
}

static void test_item_gates_and_local(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[ItemHotkeys]\nUsePadMask = 0x800\nSamplerMs = 0\n");
    CHECK(load_module());
    g_chat = true;
    press(0, 50);
    frame();
    CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);
    g_chat = false;
    press(0, 50);
    frame();
    CHECK_EQ(bar_slot(), 0);

    press(1, 900);
    frame();
    CHECK_EQ(bar_slot(), 0);
    CHECK_EQ(kbm_itemkeys_stats()->dropped_old, 1);

    kbm_st32(PLAYER + KBM_PL_IS_LOCAL_WORD, 0);
    press(1, 50);
    frame();
    CHECK_EQ(bar_slot(), 0);
    kbm_st32(PLAYER + KBM_PL_IS_LOCAL_WORD, 1);
    frame();
    CHECK_EQ(bar_slot(), 1);

    press(0, 50);
    frame_alt();
    CHECK_EQ(bar_slot(), 1);
    frame();
    CHECK_EQ(bar_slot(), 0);
    g_mod.shutdown();
}

static void test_item_disabled_and_conflict(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[ItemHotkeys]\nItemHotkeys = 0\nSamplerMs = 0\n");
    CHECK(load_module());
    CHECK(!kbm_itemkeys_enabled());
    press(0, 50);
    frame();
    CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "turned off by the config"));

    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    log = read_log();
    CHECK(strstr(log, "item key 5 (\"5\") is also the port's ItemRight bind"));
    g_mod.shutdown();
}

static void test_item_probe(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[ItemHotkeys]\nProbeInput = 1\nSamplerMs = 0\n");
    CHECK(load_module());
    frame();
    kbm_st32(SPAD + KBM_SPAD_HELD, 0x808);
    kbm_st32(SPAD + KBM_SPAD_NEW, 0x800);
    frame();
    kbm_st32(SPAD + KBM_SPAD_HELD, 0x8);
    frame();
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "probe: pad buttons 00000000 -> 00000808 (pressed 00000808, released 00000000)"));
    CHECK(strstr(log, "probe: pad buttons 00000808 -> 00000008 (pressed 00000000, released 00000800)"));
}

static void test_item_sampler(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[ItemHotkeys]\nSamplerMs = 3\n");
    CHECK(load_module());
    g_keys[K_2] = true;
    for (int i = 0; i < 100 && bar_slot() != 1; i++) {
        usleep(5000);
        frame();
    }
    CHECK_EQ(bar_slot(), 1);
    g_keys[K_2] = false;
    usleep(30000);
    g_keys[K_1] = true;
    for (int i = 0; i < 100 && bar_slot() != 0; i++) {
        usleep(5000);
        frame();
    }
    CHECK_EQ(bar_slot(), 0);
    g_keys[K_1] = false;
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "key sampler running"));
    CHECK(strstr(log, "key sampler stopped"));
}

static void set_state(uint32_t v) { kbm_st32(STATE_OBJ + KBM_STATE_WORD, v); }
static void set_mode(uint8_t m) { *(uint8_t *)KBM_GPTR(MODE_OBJ + KBM_MODE_BYTE) = m; }

static void test_reversal_paths(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    kbm_st32(PLAYER + KBM_PL_REVERSED_WORD, 5);
    g_keys[K_W] = true;
    g_port_angle = 0x4000;
    g_port_mag = 150;
    frame();
    CHECK_EQ(stick_angle(), 0xc000);
    frame_alt();
    CHECK_EQ(stick_angle(), 0x4000);
    kbm_st32(PLAYER + KBM_PL_REVERSED_WORD, 0);
    frame();
    CHECK_EQ(stick_angle(), 0x4000);
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "reversed-stick word is 5: the stick is reversed"));
    CHECK(strstr(log, "reversed-stick word is back to 0"));

    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    kbm_st32(PLAYER + KBM_PL_REVERSED_WORD, 9);
    g_keys[K_W] = true;
    frame_alt();
    CHECK_EQ(stick_angle(), 0x4000);
    g_mod.shutdown();
    log = read_log();
    CHECK(strstr(log, "ignored, the safe-area input path of the game never reverses the stick"));
}

static void test_input_blocking(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    g_keys[K_W] = true;
    g_port_angle = 0x1111;
    g_port_mag = 0;

    const uint32_t blocked[] = {0, 2, 3, 4, 5, 6, 8, 12, 26, 28, 99};
    for (size_t i = 0; i < sizeof blocked / sizeof blocked[0]; i++) {
        set_state(blocked[i]);
        kbm_st16(PLAYER + KBM_PL_STICK_ANGLE, 0);
        frame_alt();
        CHECK_EQ(stick_angle(), 0x1111);
    }
    const uint32_t accepted[] = {1, 7, 27};
    for (size_t i = 0; i < sizeof accepted / sizeof accepted[0]; i++) {
        set_state(accepted[i]);
        frame_alt();
        CHECK_EQ(stick_angle(), 0x4000);
    }
    set_state(1);

    const uint8_t stick_locked[] = {5, 6};
    for (size_t i = 0; i < 2; i++) {
        set_mode(stick_locked[i]);
        g_port_angle = 0x2222;
        frame();
        CHECK_EQ(stick_angle(), 0x2222);
        press(0, 50);
        frame();
        CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);
    }
    const uint8_t buttons_locked[] = {4, 7, 9};
    for (size_t i = 0; i < 3; i++) {
        set_mode(buttons_locked[i]);
        frame();
        CHECK_EQ(stick_angle(), 0x4000);
        press(0, 50);
        frame();
        CHECK_EQ(bar_slot(), KBM_ITEM_NO_ITEM);
    }
    set_mode(0);
    frame();
    CHECK_EQ(stick_angle(), 0x4000);
    press(0, 50);
    frame();
    CHECK_EQ(bar_slot(), 0);
    CHECK(kbm_driver_stats()->input_locked >= 2);
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "game input blocked: game state 3 does not take input"));
    CHECK(strstr(log, "game input blocked: game mode 5 locks the stick and buttons"));
    CHECK(strstr(log, "game input blocked: game mode 4 locks the buttons"));
    CHECK(strstr(log, "game input accepted again"));
}

static void test_respect_locks_off(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[Movement]\nRespectGameLocks = 0\n");
    CHECK(load_module());
    g_keys[K_W] = true;
    g_port_angle = 0x1111;
    set_state(3);
    frame_alt();
    CHECK_EQ(stick_angle(), 0x4000);
    set_state(1);
    set_mode(5);
    frame();
    CHECK_EQ(stick_angle(), 0x4000);
    g_mod.shutdown();
    char *log = read_log();
    CHECK(!strstr(log, "game input blocked"));
}

static void test_first_read_logs(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    g_keys[K_W] = true;
    set_state(7);
    set_mode(0);
    frame_alt();
    frame();
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "the game state is 7 at the first input update"));
    CHECK(strstr(log, "the game mode byte is 0 at the first input update"));
}

static void test_lock_read_failure(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    kbm_st32(KBM_GOT_STATE, 0);
    kbm_st32(KBM_GOT_MODE, 0);
    g_keys[K_W] = true;
    g_port_angle = 0x1111;
    frame();
    CHECK_EQ(stick_angle(), 0x4000);
    frame_alt();
    CHECK_EQ(stick_angle(), 0x4000);
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "could not read the game's mode byte"));
}

static void test_passthrough_mode(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n[Movement]\nMode = passthrough\n");
    CHECK(load_module());
    g_keys[K_W] = true;
    g_keys[K_D] = true;
    g_port_angle = 0x1234;
    g_port_mag = 120;
    frame();
    CHECK_EQ(stick_angle(), 0x1234);
    CHECK_EQ(stick_mag(), 120);
    CHECK_EQ(yaw_actual(), yaw_target());
    g_mod.shutdown();
}

static void other_hook(KbmCpu *c) {
    g_other_calls++;
    fnhook_call_original(c, KBM_FN_FACE_STICK);
}

static void test_chaining(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    uint32_t other = hle_register("other:face", other_hook);
    CHECK(fnhook_set(KBM_FN_FACE_STICK, other));
    CHECK(load_module());
    CHECK(fnhook_get(KBM_FN_FACE_STICK) != other);
    g_keys[K_W] = true;
    g_port_angle = 0x4000;
    g_port_mag = 150;
    frame();
    CHECK_EQ(g_other_calls, 1);
    CHECK_EQ(yaw_actual(), yaw_target());
    g_mod.shutdown();
    CHECK_EQ(fnhook_get(KBM_FN_FACE_STICK), other);
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
    char *log = read_log();
    CHECK(strstr(log, "already has a detour"));
    CHECK(strstr(log, "previous detour"));
}

static void third_party(KbmCpu *c) { fnhook_call_original(c, KBM_FN_PL_INPUT_UPDATE); }

static void test_superseded(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(load_module());
    uint32_t third = hle_register("third:input", third_party);
    fnhook_set(KBM_FN_PL_INPUT_UPDATE, third);
    g_mod.shutdown();
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), third);
    CHECK_EQ(fnhook_get(KBM_FN_FACE_STICK), 0);
    char *log = read_log();
    CHECK(strstr(log, "another hook went in after ours"));
}

static void test_declines(void) {
#ifndef MOCK_NO_DISPATCH
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    kbm_st32(KBM_FN_FACE_STICK, 0xdeadbeefu);
    CHECK(!load_module());
    CHECK(kbm_boot_state() == KBM_BOOT_FAILED);
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
    CHECK_EQ(fnhook_get(KBM_FN_FACE_STICK), 0);
    char *log = read_log();
    CHECK(strstr(log, "signature MISMATCH"));
    CHECK(strstr(log, "module is inactive"));
#endif

    reset_world();
    write_ini("[General]\nStrictVersion = 1\n");
    memset(&g_mod, 0, sizeof g_mod);
    g_host_tbl = (MhguHost){.api = MHGU_ENGINE_MODULE_API, .log = host_log, .setting = host_setting, .module_dir = g_dir,
        .port_version = "9.9.9"};
    CHECK(!mhgu_engine_module_entry(&g_host_tbl, &g_mod));
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
}

static void unload_game(void) {
    g_dispatch_ready = false;
    for (size_t i = 0; i < KBM_GUEST_SIG_COUNT; i++) {
        uint32_t zero[KBM_SIG_WORDS] = {0, 0, 0};
        put_words(kbm_guest_sigs[i].addr, zero);
    }
}

static void test_delayed_load(void) {
    reset_world();
    write_ini("[General]\nLogLevel = debug\nStatsIntervalSeconds = 0\n");
    unload_game();
    CHECK(start_module());
    CHECK(g_mod.shutdown != NULL);
    usleep(60000);
    CHECK(kbm_boot_state() == KBM_BOOT_WAITING);
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
    CHECK_EQ(fnhook_get(KBM_FN_FACE_STICK), 0);

    for (size_t i = 0; i < KBM_GUEST_SIG_COUNT; i++) put_words(kbm_guest_sigs[i].addr, kbm_guest_sigs[i].words);
    g_dispatch_ready = true;
    CHECK(wait_terminal(3000));
    CHECK(fnhook_get(KBM_FN_PL_INPUT_UPDATE) != 0);
    CHECK(fnhook_get(KBM_FN_FACE_STICK) != 0);

    g_keys[K_W] = true;
    g_port_angle = 0x4000;
    g_port_mag = 150;
    frame();
    CHECK_EQ(yaw_actual(), yaw_target());
    g_mod.shutdown();
    char *log = read_log();
    CHECK(strstr(log, "waiting for the game to load"));
    CHECK(strstr(log, "the game is loaded"));
    CHECK(strstr(log, "ready: 3 hook(s) installed"));
}

static void test_shutdown_while_waiting(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    unload_game();
    CHECK(start_module());
    usleep(30000);
    g_mod.shutdown();
    CHECK(kbm_boot_state() == KBM_BOOT_STOPPED);
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
    char *log = read_log();
    CHECK(strstr(log, "stop requested while waiting"));
    CHECK(strstr(log, "shutdown complete"));
}

static void test_wait_timeout(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\nHookWaitSeconds = 1\n");
    unload_game();
    CHECK(start_module());
    CHECK(!wait_terminal(4000));
    CHECK(kbm_boot_state() == KBM_BOOT_FAILED);
    char *log = read_log();
    CHECK(strstr(log, "did not become ready in time"));
    g_mod.shutdown();
}

static void test_disabled(void) {
    reset_world();
    write_ini("[General]\nEnabled = 0\n");
    CHECK(start_module());
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
    CHECK_EQ(fnhook_get(KBM_FN_FACE_STICK), 0);
    char *log = read_log();
    CHECK(strstr(log, "disabled by the config file"));
}

static void test_log_rotation(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\n");
    CHECK(load_module());
    g_mod.shutdown();
    CHECK(load_module());
    g_mod.shutdown();
    char path[512];
    snprintf(path, sizeof path, "%s/keyboard_movement.log.prev", g_dir);
    CHECK(access(path, F_OK) == 0);
}

#ifdef MOCK_NO_MENU_GATE
static void test_no_menu_gate(void) {
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    CHECK(!load_module());
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
    char *log = read_log();
    CHECK(strstr(log, "no way to tell that Forge's menu is open"));
    CHECK(strstr(log, "declines to load"));

    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\nRequireGates = 0\n");
    CHECK(load_module());
    CHECK(fnhook_get(KBM_FN_PL_INPUT_UPDATE) != 0);
    log = read_log();
    CHECK(strstr(log, "menu gate: unavailable"));
    g_mod.shutdown();
}
#endif

int main(void) {
    void *base = mmap((void *)KBM_GUEST_BASE, 0x08100000u, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (base != (void *)KBM_GUEST_BASE) {
        fprintf(stderr, "could not map the mock guest memory at the guest base (%p)\n", base);
        return 2;
    }
    snprintf(g_dir, sizeof g_dir, "/tmp/kbm_test_XXXXXX");
    if (!mkdtemp(g_dir)) return 2;

#ifdef MOCK_NO_MENU_GATE
    test_no_menu_gate();
    CHECK_REPORT("test_module (no menu gate)");
#endif
#ifdef MOCK_NO_DISPATCH
    test_clean_flow();
    test_delayed_load();
    test_shutdown_while_waiting();
    test_wait_timeout();
    CHECK_REPORT("test_module (no dispatch_lookup: polls memory)");
#endif
    test_clean_flow();
    if (getenv("KBM_TEST_KEEP")) {
        printf("log dir: %s\n", g_dir);
        return 0;
    }
    test_walk_and_learning();
    test_reversed_controls_and_remote();
    test_gates();
    test_snap_scope_and_deny();
    test_passthrough_mode();
    test_item_select();
    test_item_slot_mapping();
    test_item_double_press();
    test_item_use_unconfigured();
    test_item_gates_and_local();
    test_item_disabled_and_conflict();
    test_item_probe();
    test_item_sampler();
    test_reversal_paths();
    test_input_blocking();
    test_lock_read_failure();
    test_respect_locks_off();
    test_first_read_logs();
    test_smooth_turn();
    test_safe_area_hook();
    test_game_turn_mode();
    test_chaining();
    test_superseded();
    test_declines();
    test_delayed_load();
    test_shutdown_while_waiting();
    test_wait_timeout();
    test_disabled();
    test_log_rotation();
#ifdef MOCK_OVERLAY_EXPORT
    CHECK_REPORT("test_module (overlay_menu_open exported)");
#else
    CHECK_REPORT("test_module (SDL menu gate, as on 0.15.1 Windows)");
#endif
}
