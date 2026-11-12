#include "keys.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "host.h"

#define KIND_NONE 0
#define KIND_KEY 1
#define KIND_MOUSE 2

static KbmBind g_binds[KBM_B_COUNT];

static const char *const k_ini_names[KBM_B_COUNT] = {"MoveUp", "MoveDown", "MoveLeft", "MoveRight", "Walk"};
static const char *const k_defaults[KBM_B_COUNT] = {"W", "S", "A", "D", "Left Shift"};
static const char *const k_labels[KBM_B_COUNT] = {"up", "down", "left", "right", "walk"};

int kbm_split_keys(const char *list, char out[][KBM_TOKEN_MAX], int max) {
    int n = 0;
    const char *p = list;
    while (*p && n < max) {
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        if (!*p) break;
        size_t len = 0;
        char tok[KBM_TOKEN_MAX];
        while (*p && *p != ',' && len < KBM_TOKEN_MAX - 1) tok[len++] = *p++;
        while (*p && *p != ',') p++;
        while (len && isspace((unsigned char)tok[len - 1])) len--;
        tok[len] = 0;
        if (len) {
            memcpy(out[n], tok, len + 1);
            n++;
        }
    }
    return n;
}

bool kbm_parse_mouse_name(const char *name, int *button) {
    static const struct {
        const char *name;
        int button;
    } table[] = {{"Mouse Left", 1}, {"Mouse Middle", 2}, {"Mouse Right", 3}, {"Mouse 4", 4}, {"Mouse 5", 5}};
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (!strcasecmp(name, table[i].name)) {
            *button = table[i].button;
            return true;
        }
    }
    return false;
}

bool kbm_bind_parse(KbmBind *b, const char *label, const char *text) {
    (void)label;
    b->n = 0;
    char toks[KBM_BIND_KEYS + 2][KBM_TOKEN_MAX];
    int n = kbm_split_keys(text, toks, KBM_BIND_KEYS + 2);
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(toks[i], "None")) continue;
        int mb = 0;
        if (kbm_parse_mouse_name(toks[i], &mb)) {
            if (!g_host.sdl_mouse_state) continue;
            if (b->n < KBM_BIND_KEYS) b->refs[b->n++] = (KbmKeyRef){KIND_MOUSE, mb};
            continue;
        }
        int sc = g_host.sdl_scancode_from_name(toks[i]);
        if (sc <= 0) continue;
        if (b->n < KBM_BIND_KEYS) b->refs[b->n++] = (KbmKeyRef){KIND_KEY, sc};
    }
    return b->n > 0;
}

bool kbm_bind_uses_mouse(const KbmBind *b) {
    for (int i = 0; i < b->n; i++)
        if (b->refs[i].kind == KIND_MOUSE) return true;
    return false;
}

bool kbm_bind_has_scancode(const KbmBind *b, int scancode) {
    for (int i = 0; i < b->n; i++)
        if (b->refs[i].kind == KIND_KEY && b->refs[i].code == scancode) return true;
    return false;
}

static void parse_bind(KbmBindId id, const char *text) { kbm_bind_parse(&g_binds[id], k_labels[id], text); }

bool kbm_keys_init(const KbmConfig *cfg, KbmSettingFn setting) {
    memset(g_binds, 0, sizeof g_binds);
    for (int i = 0; i < KBM_B_COUNT; i++) {
        char text[KBM_KEY_TEXT];
        snprintf(text, sizeof text, "%s", k_defaults[i]);
        if (!cfg->keys_from_port && cfg->key_text[i][0]) {
            snprintf(text, sizeof text, "%s", cfg->key_text[i]);
        } else if (!(cfg->keys_from_port && setting && setting(k_ini_names[i], text, sizeof text))) {
            snprintf(text, sizeof text, "%s", k_defaults[i]);
        }
        parse_bind(i, text);
    }
    bool usable = true;
    for (int i = KBM_B_UP; i <= KBM_B_RIGHT; i++)
        if (!g_binds[i].n) usable = false;
    return usable;
}

bool kbm_bind_down(const KbmBind *b, const bool *ks, int nkeys, uint32_t mouse) {
    for (int i = 0; i < b->n; i++) {
        const KbmKeyRef *r = &b->refs[i];
        if (r->kind == KIND_KEY && r->code < nkeys && ks[r->code]) return true;
        if (r->kind == KIND_MOUSE && (mouse & (1u << (r->code - 1)))) return true;
    }
    return false;
}

void kbm_keys_sample(KbmKeys *out) {
    int n = 0;
    const bool *ks = g_host.sdl_keyboard_state ? g_host.sdl_keyboard_state(&n) : NULL;
    uint32_t mouse = 0;
    bool mouse_needed = false;
    for (int i = 0; i < KBM_B_COUNT; i++)
        for (int k = 0; k < g_binds[i].n; k++) mouse_needed |= g_binds[i].refs[k].kind == KIND_MOUSE;
    if (mouse_needed && g_host.sdl_mouse_state) mouse = g_host.sdl_mouse_state(NULL, NULL);
    memset(out, 0, sizeof *out);
    if (!ks) return;
    out->up = kbm_bind_down(&g_binds[KBM_B_UP], ks, n, mouse);
    out->down = kbm_bind_down(&g_binds[KBM_B_DOWN], ks, n, mouse);
    out->left = kbm_bind_down(&g_binds[KBM_B_LEFT], ks, n, mouse);
    out->right = kbm_bind_down(&g_binds[KBM_B_RIGHT], ks, n, mouse);
    out->walk = kbm_bind_down(&g_binds[KBM_B_WALK], ks, n, mouse);
}
