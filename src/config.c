#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

KbmConfig g_cfg;

static const char *const k_key_names[5] = {"Up", "Down", "Left", "Right", "Walk"};

void kbm_config_defaults(KbmConfig *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->enabled = true;
    cfg->hook_wait_s = 600;
    cfg->strict_version = false;
    cfg->require_gates = true;
    cfg->mode = KBM_MODE_DIRECT;
    cfg->socd = KBM_SOCD_LAST;
    cfg->snap_facing = true;
    cfg->turn_mode = KBM_TURN_SMOOTH;
    cfg->turn_response = 0.6;
    cfg->turn_min_deg = 12.0;
    cfg->turn_max_deg = 60.0;
    cfg->safe_area_hook = true;
    cfg->respect_locks = true;
    cfg->item_hotkeys = true;
    cfg->ik_double_ms = 350;
    cfg->ik_use_mask = 0;
    cfg->ik_use_hold = 3;
    cfg->ik_use_delay = 0;
    cfg->ik_sampler_ms = 4;
    static const char *const k_default_keys[KBM_IK_KEYS] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="};
    for (int i = 0; i < KBM_IK_KEYS; i++) {
        snprintf(cfg->ik_key_text[i], KBM_KEY_TEXT, "%s", k_default_keys[i]);
        cfg->ik_slot[i] = i;
    }
    kbm_config_finalize(cfg);
    cfg->snap_scope = KBM_SCOPE_UPDATE;
    cfg->keys_from_port = true;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

bool kbm_parse_bool(const char *text, bool *out) {
    char buf[16];
    size_t n = 0;
    while (*text && isspace((unsigned char)*text)) text++;
    while (text[n] && !isspace((unsigned char)text[n]) && n < sizeof buf - 1) {
        buf[n] = (char)tolower((unsigned char)text[n]);
        n++;
    }
    buf[n] = 0;
    if (!strcmp(buf, "1") || !strcmp(buf, "true") || !strcmp(buf, "yes") || !strcmp(buf, "on")) {
        *out = true;
        return true;
    }
    if (!strcmp(buf, "0") || !strcmp(buf, "false") || !strcmp(buf, "no") || !strcmp(buf, "off")) {
        *out = false;
        return true;
    }
    return false;
}

int kbm_parse_actlist(const char *text, KbmActId *out, int max) {
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        char *end = NULL;
        unsigned long st = strtoul(p, &end, 16);
        if (end == p || *end != ':') return -1;
        p = end + 1;
        unsigned long no = strtoul(p, &end, 10);
        if (end == p || st > 0xffff || no > 0xffff) return -1;
        out[n].st = (uint16_t)st;
        out[n].no = (uint16_t)no;
        n++;
        p = end;
    }
    return n;
}

bool kbm_act_listed(const KbmActId *list, int n, uint16_t st, uint16_t no) {
    for (int i = 0; i < n; i++)
        if (list[i].st == st && list[i].no == no) return true;
    return false;
}

static void apply(KbmConfig *cfg, const char *key, const char *val) {
    bool b;
    if (!strcasecmp(key, "Enabled")) {
        if (kbm_parse_bool(val, &b)) cfg->enabled = b;
    } else if (!strcasecmp(key, "HookWaitSeconds")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 1 && v <= 86400) cfg->hook_wait_s = (uint32_t)v;
    } else if (!strcasecmp(key, "StrictVersion")) {
        if (kbm_parse_bool(val, &b)) cfg->strict_version = b;
    } else if (!strcasecmp(key, "RequireGates")) {
        if (kbm_parse_bool(val, &b)) cfg->require_gates = b;
    } else if (!strcasecmp(key, "Mode")) {
        if (!strcasecmp(val, "direct")) cfg->mode = KBM_MODE_DIRECT;
        else if (!strcasecmp(val, "passthrough")) cfg->mode = KBM_MODE_PASSTHROUGH;
    } else if (!strcasecmp(key, "OppositeKeys")) {
        if (!strcasecmp(val, "last")) cfg->socd = KBM_SOCD_LAST;
        else if (!strcasecmp(val, "neutral")) cfg->socd = KBM_SOCD_NEUTRAL;
    } else if (!strcasecmp(key, "SnapFacing")) {
        if (kbm_parse_bool(val, &b)) cfg->snap_facing = b;
    } else if (!strcasecmp(key, "TurnMode")) {
        if (!strcasecmp(val, "smooth")) cfg->turn_mode = KBM_TURN_SMOOTH;
        else if (!strcasecmp(val, "snap")) cfg->turn_mode = KBM_TURN_SNAP;
        else if (!strcasecmp(val, "game")) cfg->turn_mode = KBM_TURN_GAME;
    } else if (!strcasecmp(key, "TurnResponse")) {
        char *end;
        double v = strtod(val, &end);
        if (end != val && v >= 0.05 && v <= 1.0) cfg->turn_response = v;
    } else if (!strcasecmp(key, "TurnMinStepDegrees")) {
        char *end;
        double v = strtod(val, &end);
        if (end != val && v >= 0.0 && v <= 90.0) cfg->turn_min_deg = v;
    } else if (!strcasecmp(key, "TurnMaxStepDegrees")) {
        char *end;
        double v = strtod(val, &end);
        if (end != val && v >= 0.0 && v <= 360.0) cfg->turn_max_deg = v;
    } else if (!strcasecmp(key, "RespectGameLocks")) {
        if (kbm_parse_bool(val, &b)) cfg->respect_locks = b;
    } else if (!strcasecmp(key, "SafeAreaHook")) {
        if (kbm_parse_bool(val, &b)) cfg->safe_area_hook = b;
    } else if (!strcasecmp(key, "AimWhileMoving")) {
        if (kbm_parse_bool(val, &b)) cfg->aim_while_moving = b;
    } else if (!strcasecmp(key, "ItemHotkeys")) {
        if (kbm_parse_bool(val, &b)) cfg->item_hotkeys = b;
    } else if (!strcasecmp(key, "DoublePressMs")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 0 && v <= 2000) cfg->ik_double_ms = (uint32_t)v;
    } else if (!strcasecmp(key, "UsePadMask")) {
        char *end;
        unsigned long v = strtoul(val, &end, 0);
        if (end != val && v <= 0xffffffffUL) cfg->ik_use_mask = (uint32_t)v;
    } else if (!strcasecmp(key, "UseHoldUpdates")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 1 && v <= 30) cfg->ik_use_hold = (uint32_t)v;
    } else if (!strcasecmp(key, "UseDelayUpdates")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 0 && v <= 30) cfg->ik_use_delay = (uint32_t)v;
    } else if (!strcasecmp(key, "SamplerMs")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 0 && v <= 50) cfg->ik_sampler_ms = (uint32_t)v;
    } else if (!strcasecmp(key, "SnapScope")) {
        if (!strcasecmp(val, "update")) cfg->snap_scope = KBM_SCOPE_UPDATE;
        else if (!strcasecmp(val, "all")) cfg->snap_scope = KBM_SCOPE_ALL;
    } else if (!strcasecmp(key, "SnapAllow")) {
        int n = kbm_parse_actlist(val, cfg->snap_allow, KBM_MAX_ACTS);
        if (n >= 0) cfg->n_allow = n;
    } else if (!strcasecmp(key, "SnapDeny")) {
        int n = kbm_parse_actlist(val, cfg->snap_deny, KBM_MAX_ACTS);
        if (n >= 0) cfg->n_deny = n;
    } else if (!strcasecmp(key, "KeysFromPort")) {
        if (kbm_parse_bool(val, &b)) cfg->keys_from_port = b;
    } else {
        for (int i = 0; i < 5; i++) {
            if (!strcasecmp(key, k_key_names[i])) {
                snprintf(cfg->key_text[i], KBM_KEY_TEXT, "%s", val);
                return;
            }
        }
        int n = 0;
        if (sscanf(key, "ItemKey%d", &n) == 1 && n >= 1 && n <= KBM_IK_KEYS) {
            snprintf(cfg->ik_key_text[n - 1], KBM_KEY_TEXT, "%s", val);
            return;
        }
        if (sscanf(key, "ItemSlot%d", &n) == 1 && n >= 1 && n <= KBM_IK_KEYS) {
            int slot = 0;
            if (kbm_ik_parse_slot(val, &slot)) cfg->ik_slot[n - 1] = slot;
            return;
        }
    }
}

bool kbm_config_load(KbmConfig *cfg, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!*s || *s == ';' || *s == '#' || *s == '[') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        apply(cfg, trim(s), trim(eq + 1));
    }
    fclose(f);
    kbm_config_finalize(cfg);
    return true;
}

void kbm_config_finalize(KbmConfig *cfg) {
    if (cfg->turn_max_deg > 0.0 && cfg->turn_max_deg < cfg->turn_min_deg) cfg->turn_max_deg = cfg->turn_min_deg;
    double r = cfg->turn_response * 65536.0;
    cfg->turn.response_q16 = r >= 65536.0 ? 65536u : (r < 1.0 ? 1u : (uint32_t)r);
    cfg->turn.min_step = kbm_degrees_to_units(cfg->turn_min_deg);
    cfg->turn.max_step = cfg->turn_max_deg > 0.0 ? kbm_degrees_to_units(cfg->turn_max_deg) : 0;
}
