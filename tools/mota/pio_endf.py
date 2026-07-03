"""
PlatformIO post-build extra-script: append the MeshCore ``EndF`` trailer to the
firmware image so a running node can self-locate its size/identity (docs/ota_protocol.md §2).

Wire it (ONLY for OTA-enabled builds) from a variant/env, e.g.:

    extra_scripts =
      ${nrf52_base.extra_scripts}
      post:tools/mota/pio_endf.py

and define ``-D ENABLE_OTA=1``. With ENABLE_OTA unset this script is a no-op, so it is safe to
leave wired everywhere.

The byte logic is the same `motalib.ensure_endf` exercised by `endf.py` and the unit tests.

ESP32 / RP2040 emit ${PROGNAME}.bin (the raw app image) -> EndF appended to the .bin.
nRF52 emits ${PROGNAME}.hex (the app, for DFU/UF2) -> EndF appended into the .hex right after the
app's last byte (so the downstream .uf2 / DFU .zip carry it). Both feed the same on-device EndF scan.
"""

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

import os
import sys

sys.path.insert(0, os.path.join(env["PROJECT_DIR"], "tools", "mota"))  # noqa: F821
import motalib as ml


def _ota_enabled() -> bool:
    for d in env.get("CPPDEFINES", []):  # noqa: F821
        name = d[0] if isinstance(d, (list, tuple)) else d
        if name == "ENABLE_OTA":
            return True
    return False


def _is_nrf52() -> bool:
    for d in env.get("CPPDEFINES", []):  # noqa: F821
        name = d[0] if isinstance(d, (list, tuple)) else d
        if name == "NRF52_PLATFORM":
            return True
    return False


def _cppdef(name):                                # value of a -D<name>=<value> build flag, or None
    for d in env.get("CPPDEFINES", []):           # noqa: F821
        if isinstance(d, (list, tuple)) and len(d) > 1 and d[0] == name:
            return str(d[1])
        if d == name:
            return ""
    return None


def _version_from_headers():
    """FIRMWARE_VERSION is a header ``#define`` in the example (upstream MeshCore convention), not a -D, so
    _cppdef() can't see it and the EndF version would otherwise default to 0. Read it from the source WITHOUT
    changing where MeshCore authors it: prefer the example this env actually builds (from build_src_filter),
    else fall back to the repo-wide value when it's unambiguous. Purely additive — no MeshCore file changes,
    and a -D override (checked first in _firmware_ident) still wins for release builds."""
    import re, glob
    proj = env["PROJECT_DIR"]                                 # noqa: F821
    pat = re.compile(r'#\s*define\s+FIRMWARE_VERSION\s+"([^"]+)"')

    def scan(paths):                                          # {version_string: first_path_seen}
        vals = {}
        for p in paths:
            try:
                with open(p, encoding="utf-8", errors="ignore") as f:
                    for line in f:
                        mm = pat.search(line)
                        if mm:
                            vals.setdefault(mm.group(1), p)
            except OSError:
                pass
        return vals

    # 1) restrict to the example(s) this env compiles (build_src_filter -> examples/<name>)
    srcf = ""
    for getter in (lambda: env.GetProjectOption("build_src_filter", ""),   # noqa: F821
                   lambda: env.GetProjectOption("src_filter", ""),         # noqa: F821
                   lambda: env.get("SRC_FILTER", "")):                     # noqa: F821
        try:
            v = getter()
            srcf += " " + (" ".join(map(str, v)) if isinstance(v, (list, tuple)) else str(v))
        except Exception:
            pass
    ex_dirs = set(re.findall(r"examples[/\\]([A-Za-z0-9_]+)", srcf))
    hdrs = []
    for d in ex_dirs:
        hdrs += glob.glob(os.path.join(proj, "examples", d, "*.h"))
    vals = scan(hdrs)
    if len(vals) == 1:
        return next(iter(vals))
    # 2) fall back to the repo-wide value; use it only if all examples agree (they do today: v1.17.0)
    vals = scan(glob.glob(os.path.join(proj, "examples", "*", "*.h")))
    return next(iter(vals)) if len(vals) == 1 else ""


def _firmware_ident():
    """Self-describing identity to embed in EndF (docs/ota_protocol.md §2): target_id is computed from the
    PlatformIO env name (so it's correct even without build.sh's -D MOTA_TARGET_ID), hw_id from MOTA_HW_ID,
    fw_version from FIRMWARE_VERSION (a -D if set, else the example's header #define)."""
    import re
    target_id = ml.target_id_for_env(env["PIOENV"])           # noqa: F821
    hw_id = (_cppdef("MOTA_HW_ID") or "").replace("\\", "").strip().strip('"').strip("'")
    ver_s = (_cppdef("FIRMWARE_VERSION") or "").replace("\\", "").strip().strip('"').strip("'")
    if not ver_s:                                             # not a -D -> read the header MeshCore ships
        ver_s = _version_from_headers()
    m = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", ver_s)
    fw_version = ml.pack_version(f"{m.group(1)}.{m.group(2)}.{m.group(3) or 0}") if m else 0
    return ml.FwIdent(fw_version=fw_version, target_id=target_id, hw_id=hw_id)


def _append_endf(source, target, env):           # raw .bin path (ESP32 / RP2040)
    path = str(target[0])
    with open(path, "rb") as f:
        data = f.read()
    ident = _firmware_ident()
    out, h8 = ml.ensure_endf(data, ident)
    if len(out) != len(data):
        with open(path, "wb") as f:
            f.write(out)
        print(f"EndF: appended to {os.path.basename(path)} (body_len={len(data)} body_hash={h8.hex()} "
              f"target={ident.target_id:#010x} hw='{ident.hw_id}' fw={ident.fw_version:#010x})")
    else:
        print(f"EndF: already present in {os.path.basename(path)} (no change)")


def _append_endf_hex(source, target, env):        # Intel-HEX path (nRF52: app for DFU/UF2)
    from intelhex import IntelHex
    path = str(target[0])
    ih = IntelHex(path)
    segs = ih.segments()
    if not segs:
        print("EndF: empty .hex, skipping"); return
    app_start, app_end = segs[0]                  # first (lowest) segment = the application image
    body = bytes(ih.tobinarray(start=app_start, size=app_end - app_start))
    ident = _firmware_ident()
    out, h8 = ml.ensure_endf(body, ident)
    if len(out) == len(body):
        print(f"EndF: already present in {os.path.basename(path)} (no change)"); return
    trailer = out[len(body):]                      # the EndF trailer (60 bytes with identity)
    for i, b in enumerate(trailer):
        ih[app_end + i] = b                        # write it right after the app's last byte
    ih.write_hex_file(path)
    print(f"EndF: appended to {os.path.basename(path)} at 0x{app_end:X} "
          f"(app=0x{app_start:X}.. body_len={len(body)} body_hash={h8.hex()} "
          f"target={ident.target_id:#010x} hw='{ident.hw_id}' fw={ident.fw_version:#010x})")


if _ota_enabled():
    if _is_nrf52():
        env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", _append_endf_hex)  # noqa: F821
    else:
        env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _append_endf)      # noqa: F821
else:
    print("EndF: ENABLE_OTA not defined; skipping trailer injection")
