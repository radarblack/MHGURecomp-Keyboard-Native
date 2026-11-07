#!/usr/bin/env bash
set -u

port_runtime="${1:-}"
here="$(cd "$(dirname "$0")/.." && pwd)"
failures=0

if [ -z "$port_runtime" ] || [ ! -d "$port_runtime" ]; then
    echo "usage: tools/check_port.sh <path to MHGURecomp's runtime directory>"
    exit 2
fi

ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; failures=$((failures + 1)); }

strip_api() { tr -d '\r' < "$1" | sed -e 's://.*$::' -e 's/[[:space:]]*$//' | grep -v '^$'; }
if diff -q <(strip_api "$here/include/engine_module_api.h") <(strip_api "$port_runtime/engine_module_api.h") >/dev/null; then
    ok "engine_module_api.h matches the port's (comments and line endings ignored)"
else
    bad "engine_module_api.h differs from the port's: re-copy it, check what changed, rebuild"
fi

need() {
    local file="$1" pattern="$2" what="$3"
    if [ -f "$port_runtime/$file" ] && grep -Eq "$pattern" "$port_runtime/$file"; then
        ok "$what"
    else
        bad "$what: not found in $file"
    fi
}

need rt.h 'bool fnhook_set\(uint32_t addr, uint32_t detour\)' "fnhook_set(addr, detour)"
need rt.h 'uint32_t fnhook_get\(uint32_t addr\)' "fnhook_get(addr)"
need rt.h 'void fnhook_call_original\(Cpu \*c, uint32_t addr\)' "fnhook_call_original(c, addr)"
need rt.h 'uint32_t hle_register\(const char \*name, HostFn fn\)' "hle_register(name, fn)"
need rt.h 'typedef void \(\*HostFn\)\(Cpu \*c\)' "HostFn is void(Cpu *)"
need rt.h 'void guest_call\(Cpu \*c, uint32_t addr\)' "guest_call(c, addr)"
need cpu.h 'uint32_t r\[16\];' "Cpu starts with uint32_t r[16]"
need cpu.h 'define GUEST_BASE 0x10000000000ull' "GUEST_BASE is 1 TB on desktop"
need textinput.h 'bool textinput_active\(void\)' "textinput_active()"
need hle/chat.h 'bool chat_active\(void\)' "chat_active()"
need config.c '"MoveUp"' "ini key MoveUp"
need config.c '"MoveDown"' "ini key MoveDown"
need config.c '"MoveLeft"' "ini key MoveLeft"
need config.c '"MoveRight"' "ini key MoveRight"
need config.c '"Walk"' "ini key Walk"

if [ -f "$port_runtime/cpu.h" ] && grep -Eq 'offsetof\(Cpu, pc\) == 64' "$port_runtime/cpu.h"; then
    ok "Cpu layout assertions unchanged"
else
    bad "Cpu layout assertion for pc not found"
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "port compatibility: all checks passed"
    exit 0
fi
echo "port compatibility: $failures check(s) failed"
exit 1
