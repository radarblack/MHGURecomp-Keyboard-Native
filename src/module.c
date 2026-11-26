#include <stdio.h>
#include <string.h>

#include "boot.h"
#include "config.h"
#include "driver.h"
#include "engine_module_api.h"
#include "hooks.h"
#include "host.h"
#include "itemkeys.h"
#include "keys.h"
#include "platform.h"
#include "trace.h"

#ifndef KBM_VERSION
#define KBM_VERSION "0.1.0"
#endif

#define KBM_INI_NAME "BetterKeyboard.ini"

static bool g_loaded;
static bool g_shutdown_done;

MHGU_ENGINE_MODULE_EXPORT const uint32_t mhgu_engine_module_api = MHGU_ENGINE_MODULE_API;

static void abort_load(void) {
    kbm_hooks_remove();
    kbm_trace_close();
}

static void module_shutdown(void) {
    if (g_shutdown_done) return;
    g_shutdown_done = true;
    kbm_log("shutting down");
    kbm_boot_stop();
    kbm_itemkeys_stop();
    kbm_hooks_remove();
    kbm_trace_close();
    kbm_log("shutdown complete");
    g_loaded = false;
}

MHGU_ENGINE_MODULE_EXPORT bool mhgu_engine_module_entry(const MhguHost *host, MhguEngineModule *out) {
    g_shutdown_done = false;
    g_loaded = false;

    kbm_log_set(host->log);
    kbm_log("version %s, port %s", KBM_VERSION, host->port_version ? host->port_version : "unknown");

    kbm_config_defaults(&g_cfg);
    char ini[1400];
    snprintf(ini, sizeof ini, "%s/%s", host->module_dir ? host->module_dir : ".", KBM_INI_NAME);
    if (kbm_config_load(&g_cfg, ini)) kbm_log("config loaded from %s", ini);
    else kbm_log("no config at %s, using the built-in defaults", ini);

    if (!g_cfg.enabled) {
        kbm_log("Enabled = 0: the module stays idle");
        if (out) out->name = "BetterKeyboard (disabled)";
        return true;
    }

    if (!kbm_host_resolve(g_cfg.require_gates)) {
        kbm_log("the port does not provide everything the module needs: declining to load");
        abort_load();
        return false;
    }
    if (!kbm_keys_init(host->setting)) {
        kbm_log("a movement key could not be resolved: declining to load");
        abort_load();
        return false;
    }

    kbm_itemkeys_init();
    kbm_driver_reset();
    kbm_trace_init(host->module_dir);
    if (!kbm_boot_start()) {
        kbm_log("the boot thread could not start: declining to load");
        abort_load();
        return false;
    }

    if (out) {
        out->name = "BetterKeyboard";
        out->shutdown = module_shutdown;
    }
    g_loaded = true;
    kbm_log("loaded, waiting for the game to be ready");
    return true;
}
