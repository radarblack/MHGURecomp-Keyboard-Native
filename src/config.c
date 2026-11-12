#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

KbmConfig g_cfg;

static const char *const k_key_names[5] = {"Up", "Down", "Left", "Right", "Walk"};

void kbm_config_defaults(KbmConfig *cfg, KbmLogLevel default_level) {
    memset(cfg, 0, sizeof *cfg);
    cfg->enabled = true;
    cfg->log_level = default_level;
    cfg->log_file = true;
    cfg->stats_interval_s = 10;
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
    cfg->probe_input = false;
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

static void apply(KbmConfig *cfg, const char *key, const char *val, int line) {
    bool b;
    KbmLogLevel lv;
    if (!strcasecmp(key, "Enabled")) {
        if (kbm_parse_bool(val, &b)) cfg->enabled = b;
        else goto bad;
    } else if (!strcasecmp(key, "LogLevel")) {
        if (kbm_log_level_parse(val, &lv)) cfg->log_level = lv;
        else goto bad;
    } else if (!strcasecmp(key, "LogFile")) {
        if (kbm_parse_bool(val, &b)) cfg->log_file = b;
        else goto bad;
    } else if (!strcasecmp(key, "StatsIntervalSeconds")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 0 && v <= 3600) cfg->stats_interval_s = (uint32_t)v;
        else goto bad;
    } else if (!strcasecmp(key, "HookWaitSeconds")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 1 && v <= 86400) cfg->hook_wait_s = (uint32_t)v;
        else goto bad;
    } else if (!strcasecmp(key, "StrictVersion")) {
        if (kbm_parse_bool(val, &b)) cfg->strict_version = b;
        else goto bad;
    } else if (!strcasecmp(key, "RequireGates")) {
        if (kbm_parse_bool(val, &b)) cfg->require_gates = b;
        else goto bad;
    } else if (!strcasecmp(key, "Mode")) {
        if (!strcasecmp(val, "direct")) cfg->mode = KBM_MODE_DIRECT;
        else if (!strcasecmp(val, "passthrough")) cfg->mode = KBM_MODE_PASSTHROUGH;
        else goto bad;
    } else if (!strcasecmp(key, "OppositeKeys")) {
        if (!strcasecmp(val, "last")) cfg->socd = KBM_SOCD_LAST;
        else if (!strcasecmp(val, "neutral")) cfg->socd = KBM_SOCD_NEUTRAL;
        else goto bad;
    } else if (!strcasecmp(key, "SnapFacing")) {
        if (kbm_parse_bool(val, &b)) cfg->snap_facing = b;
        else goto bad;
    } else if (!strcasecmp(key, "TurnMode")) {
        if (!strcasecmp(val, "smooth")) cfg->turn_mode = KBM_TURN_SMOOTH;
        else if (!strcasecmp(val, "snap")) cfg->turn_mode = KBM_TURN_SNAP;
        else if (!strcasecmp(val, "game")) cfg->turn_mode = KBM_TURN_GAME;
        else goto bad;
    } else if (!strcasecmp(key, "TurnResponse")) {
        char *end;
        double v = strtod(val, &end);
        if (end != val && v >= 0.05 && v <= 1.0) cfg->turn_response = v;
        else goto bad;
    } else if (!strcasecmp(key, "TurnMinStepDegrees")) {
        char *end;
        double v = strtod(val, &end);
        if (end != val && v >= 0.0 && v <= 90.0) cfg->turn_min_deg = v;
        else goto bad;
    } else if (!strcasecmp(key, "TurnMaxStepDegrees")) {
        char *end;
        double v = strtod(val, &end);
        if (end != val && v >= 0.0 && v <= 360.0) cfg->turn_max_deg = v;
        else goto bad;
    } else if (!strcasecmp(key, "RespectGameLocks")) {
        if (kbm_parse_bool(val, &b)) cfg->respect_locks = b;
        else goto bad;
    } else if (!strcasecmp(key, "SafeAreaHook")) {
        if (kbm_parse_bool(val, &b)) cfg->safe_area_hook = b;
        else goto bad;
    } else if (!strcasecmp(key, "ItemHotkeys")) {
        if (kbm_parse_bool(val, &b)) cfg->item_hotkeys = b;
        else goto bad;
    } else if (!strcasecmp(key, "DoublePressMs")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 0 && v <= 2000) cfg->ik_double_ms = (uint32_t)v;
        else goto bad;
    } else if (!strcasecmp(key, "UsePadMask")) {
        char *end;
        unsigned long v = strtoul(val, &end, 0);
        if (end != val && v <= 0xffffffffUL) cfg->ik_use_mask = (uint32_t)v;
        else goto bad;
    } else if (!strcasecmp(key, "UseHoldUpdates")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 1 && v <= 30) cfg->ik_use_hold = (uint32_t)v;
        else goto bad;
    } else if (!strcasecmp(key, "UseDelayUpdates")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 0 && v <= 30) cfg->ik_use_delay = (uint32_t)v;
        else goto bad;
    } else if (!strcasecmp(key, "SamplerMs")) {
        char *end;
        long v = strtol(val, &end, 10);
        if (end != val && v >= 0 && v <= 50) cfg->ik_sampler_ms = (uint32_t)v;
        else goto bad;
    } else if (!strcasecmp(key, "ProbeInput")) {
        if (kbm_parse_bool(val, &b)) cfg->probe_input = b;
        else goto bad;
    } else if (!strcasecmp(key, "SnapScope")) {
        if (!strcasecmp(val, "update")) cfg->snap_scope = KBM_SCOPE_UPDATE;
        else if (!strcasecmp(val, "all")) cfg->snap_scope = KBM_SCOPE_ALL;
        else goto bad;
    } else if (!strcasecmp(key, "SnapAllow")) {
        int n = kbm_parse_actlist(val, cfg->snap_allow, KBM_MAX_ACTS);
        if (n >= 0) cfg->n_allow = n;
        else goto bad;
    } else if (!strcasecmp(key, "SnapDeny")) {
        int n = kbm_parse_actlist(val, cfg->snap_deny, KBM_MAX_ACTS);
        if (n >= 0) cfg->n_deny = n;
        else goto bad;
    } else if (!strcasecmp(key, "KeysFromPort")) {
        if (kbm_parse_bool(val, &b)) cfg->keys_from_port = b;
        else goto bad;
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
            else goto bad;
            return;
        }
        KBM_LOG(KBM_LOG_WARN, "config line %d: unknown key '%s' ignored", line, key);
    }
    return;
bad:
    KBM_LOG(KBM_LOG_WARN, "config line %d: bad value '%s' for '%s', keeping the default", line, val, key);
}

bool kbm_config_load(KbmConfig *cfg, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char line[512];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        char *s = trim(line);
        if (!*s || *s == ';' || *s == '#' || *s == '[') continue;
        char *eq = strchr(s, '=');
        if (!eq) {
            KBM_LOG(KBM_LOG_WARN, "config line %d: no '=' found, ignored", n);
            continue;
        }
        *eq = 0;
        apply(cfg, trim(s), trim(eq + 1), n);
    }
    fclose(f);
    kbm_config_finalize(cfg);
    return true;
}

void kbm_config_finalize(KbmConfig *cfg) {
    if (cfg->turn_max_deg > 0.0 && cfg->turn_max_deg < cfg->turn_min_deg) {
        KBM_LOG(KBM_LOG_WARN, "TurnMaxStepDegrees (%.1f) is below TurnMinStepDegrees (%.1f): using the minimum as the maximum",
            cfg->turn_max_deg, cfg->turn_min_deg);
        cfg->turn_max_deg = cfg->turn_min_deg;
    }
    double r = cfg->turn_response * 65536.0;
    cfg->turn.response_q16 = r >= 65536.0 ? 65536u : (r < 1.0 ? 1u : (uint32_t)r);
    cfg->turn.min_step = kbm_degrees_to_units(cfg->turn_min_deg);
    cfg->turn.max_step = cfg->turn_max_deg > 0.0 ? kbm_degrees_to_units(cfg->turn_max_deg) : 0;
}

static const char *turn_mode_name(KbmTurnMode m) { return m == KBM_TURN_SMOOTH ? "smooth" : m == KBM_TURN_SNAP ? "snap" : "game"; }

static const char *mode_name(KbmMode m) { return m == KBM_MODE_DIRECT ? "direct" : "passthrough"; }

void kbm_config_log(const KbmConfig *cfg) {
    KBM_LOG(KBM_LOG_INFO, "config: enabled=%d mode=%s opposite_keys=%s snap_facing=%d snap_scope=%s log_level=%s log_file=%d",
        cfg->enabled, mode_name(cfg->mode), cfg->socd == KBM_SOCD_LAST ? "last" : "neutral", cfg->snap_facing,
        cfg->snap_scope == KBM_SCOPE_UPDATE ? "update" : "all", kbm_log_level_name(cfg->log_level), cfg->log_file);
    KBM_LOG(KBM_LOG_INFO, "config: turn_mode=%s response=%.2f min_step=%.1f deg (%u units) max_step=%.1f deg (%u units) safe_area_hook=%d respect_locks=%d",
        cfg->snap_facing ? turn_mode_name(cfg->turn_mode) : "game (SnapFacing = 0)", cfg->turn_response, cfg->turn_min_deg,
        (unsigned)cfg->turn.min_step, cfg->turn_max_deg, (unsigned)cfg->turn.max_step, cfg->safe_area_hook, cfg->respect_locks);
    KBM_LOG(KBM_LOG_INFO, "config: item_hotkeys=%d double_press=%ums use_mask=0x%x hold=%u delay=%u sampler=%ums probe_input=%d",
        cfg->item_hotkeys, (unsigned)cfg->ik_double_ms, (unsigned)cfg->ik_use_mask, (unsigned)cfg->ik_use_hold,
        (unsigned)cfg->ik_use_delay, (unsigned)cfg->ik_sampler_ms, cfg->probe_input);
    KBM_LOG(KBM_LOG_INFO,
        "config: require_gates=%d strict_version=%d stats_interval=%us hook_wait=%us keys_from_port=%d snap_allow=%d snap_deny=%d",
        cfg->require_gates, cfg->strict_version, (unsigned)cfg->stats_interval_s, (unsigned)cfg->hook_wait_s, cfg->keys_from_port,
        cfg->n_allow, cfg->n_deny);
}
