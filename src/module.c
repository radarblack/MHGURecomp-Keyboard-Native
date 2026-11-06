#include <stdio.h>
#include <string.h>

#include "config.h"
#include "driver.h"
#include "engine_module_api.h"
#include "hooks.h"
#include "host.h"
#include "keys.h"
#include "log.h"
#include "platform.h"

#ifndef KBM_VERSION
#define KBM_VERSION "0.1.0"
#endif
#ifndef KBM_DEFAULT_LOG_LEVEL
#define KBM_DEFAULT_LOG_LEVEL KBM_LOG_INFO
#endif

#define KBM_MODULE_NAME "keyboard_movement"
#define KBM_INI_NAME "keyboard_movement.ini"
#define KBM_LOG_NAME "keyboard_movement.log"

static const char *const k_tested_ports[] = {"0.14."};

static bool g_loaded;
static bool g_shutdown_done;

MHGU_ENGINE_MODULE_EXPORT const uint32_t mhgu_engine_module_api = MHGU_ENGINE_MODULE_API;

static bool port_version_tested(const char *v) {
    if (!v) return false;
    for (size_t i = 0; i < sizeof k_tested_ports / sizeof k_tested_ports[0]; i++)
        if (!strncmp(v, k_tested_ports[i], strlen(k_tested_ports[i]))) return true;
    return false;
}

static void abort_load(const char *why) {
    KBM_LOG(KBM_LOG_ERROR, "module declines to load: %s", why);
    kbm_hooks_remove();
    kbm_log_close();
}

static void module_shutdown(void) {
    if (g_shutdown_done) return;
    g_shutdown_done = true;
    KBM_LOG(KBM_LOG_INFO, "shutdown: clean exit requested by the port");
    kbm_driver_log_stats(KBM_LOG_INFO, "final");
    kbm_hooks_remove();
    int left = kbm_hooks_installed_count();
    if (left) KBM_LOG(KBM_LOG_ERROR, "shutdown: %d hook(s) still marked installed", left);
    else KBM_LOG(KBM_LOG_INFO, "shutdown: all hooks released");
    KBM_LOG(KBM_LOG_INFO, "shutdown complete");
    g_loaded = false;
    kbm_log_close();
}

MHGU_ENGINE_MODULE_EXPORT bool mhgu_engine_module_entry(const MhguHost *host, MhguEngineModule *out) {
    g_shutdown_done = false;
    g_loaded = false;

    kbm_config_defaults(&g_cfg, KBM_DEFAULT_LOG_LEVEL);
    char ini[1400];
    snprintf(ini, sizeof ini, "%s/%s", host->module_dir, KBM_INI_NAME);
    kbm_log_open(host->log, host->module_dir, KBM_LOG_NAME, KBM_DEFAULT_LOG_LEVEL, true);
    bool have_ini = kbm_config_load(&g_cfg, ini);
    kbm_log_set_level(g_cfg.log_level);
    if (!g_cfg.log_file) {
        KBM_LOG(KBM_LOG_INFO, "LogFile = 0: the log goes to the port's log only");
        kbm_log_file_close();
    }

    KBM_LOG(KBM_LOG_INFO, "keyboard movement %s starting (built %s %s, %s)", KBM_VERSION, __DATE__, __TIME__, kbm_platform_name());
    KBM_LOG(KBM_LOG_INFO, "host api %u, module api %u, port version \"%s\"", host->api, (unsigned)MHGU_ENGINE_MODULE_API,
        host->port_version ? host->port_version : "(none)");
    KBM_LOG(KBM_LOG_INFO, "module folder: %s", host->module_dir);
    if (have_ini) KBM_LOG(KBM_LOG_INFO, "config file read: %s", ini);
    else KBM_LOG(KBM_LOG_INFO, "no config file at %s: using the defaults", ini);
    kbm_config_log(&g_cfg);

    if (!g_cfg.enabled) {
        KBM_LOG(KBM_LOG_INFO, "disabled by the config file (Enabled = 0): nothing is hooked");
        if (out) out->name = "Keyboard Movement (disabled)";
        kbm_log_close();
        return true;
    }

    if (port_version_tested(host->port_version)) {
        KBM_LOG(KBM_LOG_INFO, "port version is on the tested list");
    } else if (g_cfg.strict_version) {
        abort_load("StrictVersion is on and this port version has not been tested with the module");
        return false;
    } else {
        KBM_LOG(KBM_LOG_WARN, "port version \"%s\" is not on the tested list: relying on the signature and symbol checks",
            host->port_version ? host->port_version : "(none)");
    }

    if (!kbm_host_resolve(g_cfg.require_gates)) {
        abort_load("a required port or SDL symbol is missing (see the lines above)");
        return false;
    }
    if (!kbm_guest_verify()) {
        abort_load("the game code does not match what the module was written for (see the lines above)");
        return false;
    }
    if (!kbm_keys_init(&g_cfg, host->setting)) {
        abort_load("the movement keys could not be resolved");
        return false;
    }

    kbm_driver_reset();
    if (!kbm_hooks_install()) {
        abort_load("a hook could not be installed");
        return false;
    }

    if (out) {
        out->name = "Keyboard Movement";
        out->shutdown = module_shutdown;
    }
    g_loaded = true;
    KBM_LOG(KBM_LOG_INFO, "ready: %d hook(s) installed, mode=%s", kbm_hooks_installed_count(),
        g_cfg.mode == KBM_MODE_DIRECT ? "direct" : "passthrough");
    return true;
}

#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_DETACH && g_loaded && !g_shutdown_done)
        kbm_log_detach_note("DETACH: the DLL is being unloaded without shutdown() having run (crash or abnormal exit)");
    return TRUE;
}
#else
__attribute__((destructor)) static void kbm_unload_note(void) {
    if (g_loaded && !g_shutdown_done)
        kbm_log_detach_note("DETACH: the library is being unloaded without shutdown() having run (crash or abnormal exit)");
}
#endif
