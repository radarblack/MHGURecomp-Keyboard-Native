#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "check.h"
#include "engine_module_api.h"
#include "guest.h"
#include "host.h"

#define PLAYER 0x08000000u
#define HLE_START 0xf0000000u
#define MAX_HLE 16
#define MAX_HOOK 16
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
    if (addr == KBM_FN_PL_INPUT_UPDATE) {
        kbm_st16(pl + KBM_PL_STICK_ANGLE, (uint16_t)g_port_angle);
        kbm_st16(pl + KBM_PL_STICK_MAG, (uint16_t)g_port_mag);
    } else if (addr == KBM_FN_FACE_STICK) {
        uint16_t angle = kbm_ld16(pl + KBM_PL_STICK_ANGLE);
        kbm_st32(pl + KBM_PL_YAW_TARGET, (g_camera_yaw + angle + 0xc000u) & 0xffffu);
    }
}

MOCK_EXPORT void guest_call(KbmCpu *c, uint32_t addr) {
    if (addr >= HLE_START && addr < HLE_START + 16u * MAX_HLE) {
        KbmHostFn fn = g_hle[(addr - HLE_START) / 16].fn;
        if (fn) fn(c);
    }
}

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
    } t[] = {{"W", 26}, {"S", 22}, {"A", 4}, {"D", 7}, {"Left Shift", 225}, {"Up", 82}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcasecmp(name, t[i].n)) return t[i].c;
    return 0;
}
MOCK_EXPORT uint32_t SDL_GetMouseState(float *x, float *y) {
    (void)x;
    (void)y;
    return 0;
}

enum { K_W = 26, K_S = 22, K_A = 4, K_D = 7, K_SHIFT = 225 };

static void host_log(const char *fmt, ...) { (void)fmt; }

static bool host_setting(const char *key, char *out, size_t n) {
    static const struct {
        const char *k, *v;
    } t[] = {{"MoveUp", "W"}, {"MoveDown", "S"}, {"MoveLeft", "A"}, {"MoveRight", "D"}, {"Walk", "Left Shift"}};
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
    fclose(f);
}

static bool load_module(void) {
    memset(&g_mod, 0, sizeof g_mod);
    g_host_tbl = (MhguHost){.api = MHGU_ENGINE_MODULE_API, .log = host_log, .setting = host_setting, .module_dir = g_dir,
        .port_version = "0.14.0-9-gabcdef0"};
    return mhgu_engine_module_entry(&g_host_tbl, &g_mod);
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
    CHECK_EQ(fnhook_get(KBM_FN_FACE_STICK), 0);
    char *log = read_log();
    CHECK(strstr(log, "hook keyboard_movement:player_input: installed"));
    CHECK(strstr(log, "readback verified"));
    CHECK(strstr(log, "first call arrived"));
    CHECK(strstr(log, "direction reversal"));
    CHECK(strstr(log, "snap yaw"));
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
    reset_world();
    write_ini("[General]\nLogLevel = info\nStatsIntervalSeconds = 0\n");
    kbm_st32(KBM_FN_FACE_STICK, 0xdeadbeefu);
    CHECK(!load_module());
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
    CHECK_EQ(fnhook_get(KBM_FN_FACE_STICK), 0);
    char *log = read_log();
    CHECK(strstr(log, "signature MISMATCH"));
    CHECK(strstr(log, "declines to load"));

    reset_world();
    write_ini("[General]\nStrictVersion = 1\n");
    g_host_tbl.port_version = "9.9.9";
    memset(&g_mod, 0, sizeof g_mod);
    g_host_tbl = (MhguHost){.api = MHGU_ENGINE_MODULE_API, .log = host_log, .setting = host_setting, .module_dir = g_dir,
        .port_version = "9.9.9"};
    CHECK(!mhgu_engine_module_entry(&g_host_tbl, &g_mod));
    CHECK_EQ(fnhook_get(KBM_FN_PL_INPUT_UPDATE), 0);
}

static void test_disabled(void) {
    reset_world();
    write_ini("[General]\nEnabled = 0\n");
    CHECK(load_module());
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
    test_chaining();
    test_superseded();
    test_declines();
    test_disabled();
    test_log_rotation();
#ifdef MOCK_OVERLAY_EXPORT
    CHECK_REPORT("test_module (overlay_menu_open exported)");
#else
    CHECK_REPORT("test_module (SDL menu gate, as on 0.15.1 Windows)");
#endif
}
