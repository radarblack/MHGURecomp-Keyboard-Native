#!/usr/bin/env python3
import struct
import sys

try:
    import pefile
except ImportError:
    sys.exit("this tool needs pefile: pip install pefile")

PORT_REQUIRED = ["fnhook_set", "fnhook_get", "fnhook_call_original", "hle_register", "guest_call", "textinput_active", "chat_active"]
PORT_OPTIONAL = ["dispatch_lookup", "overlay_menu_open"]
SDL_REQUIRED = ["SDL_GetKeyboardState", "SDL_GetScancodeFromName"]
SDL_OPTIONAL = ["SDL_GetMouseState", "SDL_GetKeyboardFocus", "SDL_TextInputActive"]


def export_names(path):
    pe = pefile.PE(path, fast_load=True)
    entry = pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]]
    if not entry.VirtualAddress:
        return set()
    data = bytes(pe.__data__)
    rva = pe.get_offset_from_rva

    def cstr(offset):
        return data[offset:data.find(b"\0", offset)].decode("latin1")

    head = rva(entry.VirtualAddress)
    fields = struct.unpack_from("<IIHHIIIIIII", data, head)
    count, names_rva = fields[7], fields[9]
    table = rva(names_rva)
    return {cstr(rva(struct.unpack_from("<I", data, table + 4 * i)[0])) for i in range(count)}


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: check_exports.py <mhgurecomp.exe> [SDL3.dll]")
    exe = export_names(sys.argv[1])
    sdl = export_names(sys.argv[2]) if len(sys.argv) > 2 else set()
    print("%s exports %d names" % (sys.argv[1], len(exe)))
    if len(sys.argv) > 2:
        print("%s exports %d names" % (sys.argv[2], len(sdl)))
    failures = 0

    def line(kind, name, where):
        print("%-5s %-26s %s" % (kind, name, where))

    for name in PORT_REQUIRED:
        if name in exe:
            line("ok", name, "exe")
        else:
            line("FAIL", name, "required, not exported by the executable")
            failures += 1
    for name in PORT_OPTIONAL:
        line("ok" if name in exe else "info", name, "exe" if name in exe else "optional, not exported")
    for name in SDL_REQUIRED + SDL_OPTIONAL:
        where = "exe" if name in exe else "SDL3.dll" if name in sdl else None
        if where:
            line("ok", name, where)
        elif name in SDL_REQUIRED:
            line("FAIL", name, "required, found in neither file")
            failures += 1
        else:
            line("info", name, "optional, not found")

    def have(name):
        return name in exe or name in sdl

    if "overlay_menu_open" in exe:
        print("menu gate: overlay_menu_open")
    elif have("SDL_GetKeyboardFocus") and have("SDL_TextInputActive"):
        print("menu gate: SDL text-input fallback")
    else:
        print("FAIL  menu gate: neither overlay_menu_open nor the SDL fallback is available")
        failures += 1

    print()
    if failures:
        print("export check: %d failure(s)" % failures)
        sys.exit(1)
    print("export check: all required symbols are present")


main()
