#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "engine_module_api.h"
#include "guest.h"

#define EXPORT __declspec(dllexport)
#define HLE_START 0xf0000000u

typedef struct {
    uint32_t r[16];
} Cpu;
typedef void (*Fn)(Cpu *);

static Fn g_hle[16];
static const char *g_hle_name[16];
static int g_nhle;
static struct {
    uint32_t addr, detour;
} g_hk[16];
static int g_nhk;
static volatile LONG g_dispatch_ready;
static int g_fail;

#define PLAYER 0x08000000u

static void commit(uint64_t addr, uint64_t size) {
    uint64_t a = addr & ~0xFFFull, e = (addr + size + 0xFFF) & ~0xFFFull;
    if (!VirtualAlloc((void *)(uintptr_t)(KBM_GUEST_BASE + a), e - a, MEM_COMMIT, PAGE_READWRITE)) {
        printf("commit failed %lu\n", GetLastError());
        g_fail++;
    }
}

EXPORT uint32_t hle_register(const char *name, Fn fn) {
    for (int i = 0; i < g_nhle; i++)
        if (!strcmp(g_hle_name[i], name)) return HLE_START + 16u * (uint32_t)i;
    g_hle_name[g_nhle] = _strdup(name);
    g_hle[g_nhle] = fn;
    return HLE_START + 16u * (uint32_t)g_nhle++;
}

EXPORT bool fnhook_set(uint32_t addr, uint32_t detour) {
    for (int i = 0; i < g_nhk; i++)
        if (g_hk[i].addr == addr) {
            g_hk[i].detour = detour;
            return true;
        }
    g_hk[g_nhk].addr = addr;
    g_hk[g_nhk].detour = detour;
    g_nhk++;
    return true;
}

EXPORT uint32_t fnhook_get(uint32_t addr) {
    for (int i = 0; i < g_nhk; i++)
        if (g_hk[i].addr == addr) return g_hk[i].detour;
    return 0;
}

static uint32_t g_camera = 0x1000;
static int g_port_angle, g_port_mag;

EXPORT void fnhook_call_original(Cpu *c, uint32_t addr) {
    uint32_t pl = c->r[0];
    if (addr == KBM_FN_PL_INPUT_UPDATE || addr == KBM_FN_PL_INPUT_UPDATE_ALT) {
        kbm_st16(pl + KBM_PL_STICK_ANGLE, (uint16_t)g_port_angle);
        kbm_st16(pl + KBM_PL_STICK_MAG, (uint16_t)g_port_mag);
    } else if (addr == KBM_FN_FACE_STICK) {
        uint16_t a = kbm_ld16(pl + KBM_PL_STICK_ANGLE);
        kbm_st32(pl + KBM_PL_YAW_TARGET, (g_camera + a + 0xc000u) & 0xffffu);
    }
}

EXPORT void guest_call(Cpu *c, uint32_t addr) {
    if (addr >= HLE_START) g_hle[(addr - HLE_START) / 16](c);
}

EXPORT bool textinput_active(void) { return false; }
EXPORT bool chat_active(void) { return false; }
EXPORT void *dispatch_lookup(uint32_t addr) {
    (void)addr;
    return InterlockedCompareExchange(&g_dispatch_ready, 0, 0) ? (void *)1 : NULL;
}

static void host_log(const char *fmt, ...) { (void)fmt; }

static bool host_setting(const char *key, char *out, size_t n) {
    static const char *t[][2] = {{"MoveUp", "W"}, {"MoveDown", "S"}, {"MoveLeft", "A"}, {"MoveRight", "D"}, {"Walk", "Left Shift"}};
    for (int i = 0; i < 5; i++)
        if (!strcmp(key, t[i][0])) {
            snprintf(out, n, "%s", t[i][1]);
            return true;
        }
    return false;
}

#define CHECKW(c)                                       \
    do {                                                \
        if (!(c)) {                                     \
            printf("FAIL line %d: %s\n", __LINE__, #c); \
            g_fail++;                                   \
        } else printf("ok:   %s\n", #c);                \
    } while (0)

static void frame(void) {
    Cpu c;
    memset(&c, 0, sizeof c);
    c.r[0] = PLAYER;
    uint32_t addrs[2] = {KBM_FN_PL_INPUT_UPDATE, KBM_FN_FACE_STICK};
    for (int i = 0; i < 2; i++) {
        uint32_t d = fnhook_get(addrs[i]);
        if (d) g_hle[(d - HLE_START) / 16](&c);
        else fnhook_call_original(&c, addrs[i]);
    }
}

int main(void) {
    void *window = VirtualAlloc((void *)(uintptr_t)KBM_GUEST_BASE, 0x100000000ull, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
    if (window != (void *)(uintptr_t)KBM_GUEST_BASE)
        window = VirtualAlloc((void *)(uintptr_t)KBM_GUEST_BASE, 0x100000000ull, MEM_RESERVE, PAGE_READWRITE);
    CHECKW(window == (void *)(uintptr_t)KBM_GUEST_BASE);

    HMODULE sdl = LoadLibraryA("SDL3.dll");
    CHECKW(sdl != NULL);
    void (*set_key)(int, int) = (void (*)(int, int))GetProcAddress(sdl, "mock_set_key");

    CreateDirectoryA("modrun", NULL);
    {
        FILE *ini = fopen("modrun/keyboard_movement.ini", "wb");
        fputs("[Movement]\nTurnMode = snap\n", ini);
        fclose(ini);
    }
    HMODULE mod = LoadLibraryA("engine_modules/keyboard_movement/keyboard_movement.dll");
    CHECKW(mod != NULL);
    if (!mod) return 1;
    bool (*entry)(const MhguHost *, MhguEngineModule *) = (bool (*)(const MhguHost *, MhguEngineModule *))GetProcAddress(mod, "mhgu_engine_module_entry");
    CHECKW(entry != NULL);

    MhguHost host = {.api = MHGU_ENGINE_MODULE_API, .log = host_log, .setting = host_setting, .module_dir = "modrun",
        .port_version = "0.15.1"};
    MhguEngineModule out;
    memset(&out, 0, sizeof out);
    CHECKW(entry(&host, &out));
    CHECKW(out.shutdown != NULL);

    Sleep(500);
    CHECKW(fnhook_get(KBM_FN_PL_INPUT_UPDATE) == 0);

    commit(0x200000, 0x1800000);
    commit(PLAYER, 0x10000);
    for (size_t i = 0; i < KBM_GUEST_SIG_COUNT; i++)
        for (int w = 0; w < KBM_SIG_WORDS; w++) kbm_st32(kbm_guest_sigs[i].addr + 4u * (uint32_t)w, kbm_guest_sigs[i].words[w]);
    kbm_st32(PLAYER + KBM_PL_IS_LOCAL_WORD, 1);
    kbm_st32(PLAYER + KBM_PL_PHASE_WORD, 0x0100);
    kbm_st32(PLAYER + KBM_PL_YAW_ACTUAL, 0x1234);
    InterlockedExchange(&g_dispatch_ready, 1);

    for (int i = 0; i < 100 && !fnhook_get(KBM_FN_PL_INPUT_UPDATE); i++) Sleep(50);
    CHECKW(fnhook_get(KBM_FN_PL_INPUT_UPDATE) != 0);
    CHECKW(fnhook_get(KBM_FN_FACE_STICK) != 0);
    CHECKW(fnhook_get(KBM_FN_PL_INPUT_UPDATE_ALT) != 0);

    set_key(26, 1);
    g_port_angle = 0;
    g_port_mag = 0;
    frame();
    CHECKW(kbm_ld16(PLAYER + KBM_PL_STICK_ANGLE) == 0x4000);
    CHECKW(kbm_ld32(PLAYER + KBM_PL_YAW_ACTUAL) == kbm_ld32(PLAYER + KBM_PL_YAW_TARGET));
    set_key(22, 1);
    frame();
    CHECKW(kbm_ld16(PLAYER + KBM_PL_STICK_ANGLE) == 0xc000);
    CHECKW(kbm_ld32(PLAYER + KBM_PL_YAW_ACTUAL) == kbm_ld32(PLAYER + KBM_PL_YAW_TARGET));

    out.shutdown();
    CHECKW(fnhook_get(KBM_FN_PL_INPUT_UPDATE) == 0);
    CHECKW(fnhook_get(KBM_FN_PL_INPUT_UPDATE_ALT) == 0);
    CHECKW(fnhook_get(KBM_FN_FACE_STICK) == 0);
    printf(g_fail ? "WINDOWS RUN: %d FAILURES\n" : "WINDOWS RUN: all checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
