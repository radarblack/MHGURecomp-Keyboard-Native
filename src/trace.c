#include "trace.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "guest.h"
#include "host.h"
#include "keys.h"
#include "platform.h"

#define KEY_WORDS 12
#define KEY_WORDS_BASE 0x1414u
#define KEY_FLAGS_WORD 0x1444u
#define PL_WEAPON_DATA 0x13c4u
#define WEAPON_TYPE_OFFSET 0x14cu
#define IG_FIELD_A 0x3338u
#define IG_FIELD_B 0x3348u
#define IG_FIELD_C 0x334cu
#define PL_PAD_HELD 0x13f4u
#define PL_PAD_NEW 0x13f8u
#define TRACE_FILE "BetterKeyboard_trace.log"

typedef struct Snap {
    uint16_t act_st;
    uint16_t act_no;
    uint8_t phase;
    uint8_t tilt;
    uint8_t alt;
    uint16_t pad_held;
    uint32_t keys[KEY_WORDS];
    uint32_t key_flags;
    uint32_t ig_a, ig_b, ig_c;
    uint16_t item_slot;
} Snap;

static FILE *g_file;
static KbmMutex g_lock;
static bool g_lock_ready;
static uint64_t g_start_ms;
static KbmBind g_marker;
static bool g_marker_down;
static uint32_t g_marker_count;
static uint32_t g_update;
static uint32_t g_lines;
static bool g_have_prev;
static bool g_header_done;
static bool g_cap_noted;
static Snap g_prev;

static void out(const char *fmt, ...) {
    if (!g_file) return;
    uint64_t t = kbm_now_ms() - g_start_ms;
    va_list ap;
    va_start(ap, fmt);
    kbm_mutex_lock(&g_lock);
    if (g_file) {
        fprintf(g_file, "+%llu.%03llus ", (unsigned long long)(t / 1000), (unsigned long long)(t % 1000));
        vfprintf(g_file, fmt, ap);
        fputc('\n', g_file);
        fflush(g_file);
    }
    kbm_mutex_unlock(&g_lock);
    va_end(ap);
}

bool kbm_trace_init(const char *module_dir) {
    if (!g_lock_ready) {
        kbm_mutex_init(&g_lock);
        g_lock_ready = true;
    }
    kbm_trace_reset();
    if (!g_cfg.trace_actions || !module_dir) return false;
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", module_dir, TRACE_FILE);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    g_start_ms = kbm_now_ms();
    memset(&g_marker, 0, sizeof g_marker);
    bool have = g_host.sdl_scancode_from_name ? kbm_bind_parse(&g_marker, "trace marker", g_cfg.trace_marker_text) : false;
    kbm_mutex_lock(&g_lock);
    g_file = f;
    kbm_mutex_unlock(&g_lock);
    out("trace started: every change of the hunter's action, pad buttons, key words and stick tilt is logged (at most %u lines). %s",
        (unsigned)g_cfg.trace_max_lines, have ? "Press the marker key to drop a numbered marker line." : "The marker key could not be resolved.");
    return true;
}

void kbm_trace_reset(void) {
    g_marker_down = false;
    g_marker_count = 0;
    g_update = 0;
    g_lines = 0;
    g_have_prev = false;
    g_header_done = false;
    g_cap_noted = false;
    memset(&g_prev, 0, sizeof g_prev);
}

void kbm_trace_close(void) {
    if (!g_lock_ready) return;
    kbm_mutex_lock(&g_lock);
    FILE *f = g_file;
    g_file = NULL;
    kbm_mutex_unlock(&g_lock);
    if (f) {
        fprintf(f, "trace closed: %u lines, %u markers, %u updates\n", (unsigned)g_lines, (unsigned)g_marker_count, (unsigned)g_update);
        fclose(f);
    }
}

static void decode_pad(uint16_t held, char *buf, size_t n) {
    static const struct {
        uint16_t bit;
        const char *name;
    } names[] = {{0x10, "Y"}, {0x20, "X"}, {0x40, "R"}, {0x80, "B"}, {0x100, "A"}, {0x200, "ZL"}, {0x800, "ZR"}};
    size_t len = 0;
    uint16_t known = 0;
    buf[0] = 0;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (held & names[i].bit) {
            len += (size_t)snprintf(buf + len, n - len, "%s%s", len ? "+" : "", names[i].name);
            known |= names[i].bit;
        }
    }
    if (held & ~known) snprintf(buf + len, n - len, "%s%04x", len ? "+" : "", (unsigned)(held & ~known));
    if (!buf[0]) snprintf(buf, n, "-");
}

static bool poll_marker(void) {
    if (!g_marker.n || !g_host.sdl_keyboard_state) return false;
    int n = 0;
    const bool *ks = g_host.sdl_keyboard_state(&n);
    uint32_t mouse = 0;
    if (kbm_bind_uses_mouse(&g_marker) && g_host.sdl_mouse_state) mouse = g_host.sdl_mouse_state(NULL, NULL);
    bool down = ks && kbm_bind_down(&g_marker, ks, n, mouse);
    bool edge = down && !g_marker_down;
    g_marker_down = down;
    return edge;
}

static bool room(void) {
    if (g_cfg.trace_max_lines && g_lines >= g_cfg.trace_max_lines) {
        if (!g_cap_noted) {
            g_cap_noted = true;
            out("trace stopped: the line limit (TraceMaxLines = %u) was reached", (unsigned)g_cfg.trace_max_lines);
        }
        return false;
    }
    return true;
}

void kbm_trace_update(uint32_t pl, bool alt) {
    if (!g_file) return;
    g_update++;
    if (poll_marker() && room()) {
        g_marker_count++;
        g_lines++;
        out("---- marker %u ----", (unsigned)g_marker_count);
    }

    uint32_t wd = kbm_ld32(pl + PL_WEAPON_DATA);
    uint32_t wtype = wd >= 0x00200000u && wd < 0xF0000000u ? kbm_ld32(wd + WEAPON_TYPE_OFFSET) : 0xffffffffu;
    if (!g_header_done) {
        g_header_done = true;
        out("first update on the %s path, player %08x, weapon type %d (13 is the Insect Glaive)", alt ? "safe-area" : "hunt", pl, (int)wtype);
    }

    Snap s;
    memset(&s, 0, sizeof s);
    s.act_st = kbm_ld16(pl + KBM_PL_ACT_ST);
    s.act_no = kbm_ld16(pl + KBM_PL_ACT_NO);
    s.phase = (uint8_t)((kbm_ld32(pl + KBM_PL_PHASE_WORD) & 0xffffu) >> 8);
    int mag = (int16_t)kbm_ld16(pl + KBM_PL_STICK_MAG);
    s.tilt = mag < KBM_TILT_WALK_MIN ? 0 : mag < KBM_TILT_RUN_MIN ? 1 : 2;
    s.alt = alt;
    s.pad_held = kbm_ld16(pl + PL_PAD_HELD);
    for (int i = 0; i < KEY_WORDS; i++) s.keys[i] = kbm_ld32(pl + KEY_WORDS_BASE + 4u * (uint32_t)i);
    s.key_flags = kbm_ld32(pl + KEY_FLAGS_WORD);
    s.ig_a = kbm_ld32(pl + IG_FIELD_A);
    s.ig_b = kbm_ld32(pl + IG_FIELD_B);
    s.ig_c = kbm_ld32(pl + IG_FIELD_C);
    s.item_slot = kbm_ld16(pl + KBM_PL_ITEM_SLOT);

    if (g_have_prev && !memcmp(&s, &g_prev, sizeof s)) return;
    if (!room()) {
        g_prev = s;
        g_have_prev = true;
        return;
    }
    char pad[48];
    decode_pad(s.pad_held, pad, sizeof pad);
    char keys[KEY_WORDS * 9 + 1];
    size_t len = 0;
    for (int i = 0; i < KEY_WORDS; i++) len += (size_t)snprintf(keys + len, sizeof keys - len, "%s%08x", i ? " " : "", s.keys[i]);
    out("trace u=%u %c act %04x:%03u ph%u | pad %04x [%s] new %04x | tilt %u mag %d | keys %s | flags %08x | ig %08x %08x %08x | bar %u",
        (unsigned)g_update, alt ? 'a' : 'q', s.act_st, s.act_no, s.phase, s.pad_held, pad, kbm_ld16(pl + PL_PAD_NEW), s.tilt, mag, keys,
        s.key_flags, s.ig_a, s.ig_b, s.ig_c, s.item_slot);
    g_lines++;
    g_prev = s;
    g_have_prev = true;
}
