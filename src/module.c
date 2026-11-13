#include <stdio.h>
#include <string.h>

#include "boot.h"
#include "aimkeep.h"
#include "config.h"
#include "driver.h"
#include "engine_module_api.h"
#include "hooks.h"
#include "host.h"
#include "itemkeys.h"
#include "keys.h"
#include "platform.h"

#ifndef KBM_VERSION
#define KBM_VERSION "0.1.0"
#endif

#define KBM_INI_NAME "BetterKeyboard.ini"

static const char *const k_checked_ports[] = {"0.14.", "0.15.", "0.16."};

static bool g_loaded;
static bool g_shutdown_done;

MHGU_ENGINE_MODULE_EXPORT const uint32_t mhgu_engine_module_api = MHGU_ENGINE_MODULE_API;

static bool port_version_checked(const char *v) {
    if (!v) return false;
    for (size_t i = 0; i < sizeof k_checked_ports / sizeof k_checked_ports[0]; i++)
        if (!strncmp(v, k_checked_ports[i], strlen(k_checked_ports[i]))) return true;
    return false;
}

static void abort_load(void) {
    kbm_aimkeep_remove();
    kbm_hooks_remove();
}

static void module_shutdown(void) {
    if (g_shutdown_done) return;
    g_shutdown_done = true;
    kbm_boot_stop();
    kbm_itemkeys_stop();
    kbm_aimkeep_remove();
    kbm_hooks_remove();
    g_loaded = false;
}

MHGU_ENGINE_MODULE_EXPORT bool mhgu_engine_module_entry(const MhguHost *host, MhguEngineModule *out) {
    g_shutdown_done = false;
    g_loaded = false;

    kbm_config_defaults(&g_cfg);
    char ini[1400];
    snprintf(ini, sizeof ini, "%s/%s", host->module_dir, KBM_INI_NAME);
    kbm_config_load(&g_cfg, ini);

    if (!g_cfg.enabled) {
        if (out) out->name = "BetterKeyboard (disabled)";
        return true;
    }

    if (!port_version_checked(host->port_version) && g_cfg.strict_version) {
        abort_load();
        return false;
    }

    if (!kbm_host_resolve(g_cfg.require_gates)) {
        abort_load();
        return false;
    }
    if (!kbm_keys_init(&g_cfg, host->setting)) {
        abort_load();
        return false;
    }

    kbm_itemkeys_init();
    kbm_driver_reset();
    if (!kbm_boot_start()) {
        abort_load();
        return false;
    }

    if (out) {
        out->name = "BetterKeyboard";
        out->shutdown = module_shutdown;
    }
    g_loaded = true;
    return true;
}
