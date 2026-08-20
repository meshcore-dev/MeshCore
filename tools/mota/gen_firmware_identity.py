#!/usr/bin/env python3
"""Generate src/helpers/FirmwareIdentity.generated.cpp (single TU for build stamps)."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys


def target_id_for_env(env_name: str) -> int:
    return int.from_bytes(hashlib.sha256(env_name.encode()).digest()[:4], "little")


def _cpp_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def render_firmware_identity(version: str, build_date: str, target_id: int) -> str:
    return f"""// AUTO-GENERATED — do not edit. See tools/mota/gen_firmware_identity.py

#include "FirmwareIdentity.h"

namespace mesh {{

static const char kFirmwareVersion[] = "{_cpp_escape(version)}";
static const char kFirmwareBuildDate[] = "{_cpp_escape(build_date)}";
static const uint32_t kMotaTargetId = {target_id:#010x}u;

const char* firmware_version_string() {{ return kFirmwareVersion; }}
const char* firmware_build_date_string() {{ return kFirmwareBuildDate; }}
uint32_t firmware_mota_target_id() {{ return kMotaTargetId; }}

}}  // namespace mesh
"""


def write_firmware_identity(out_path: str, version: str, build_date: str, target_id: int) -> bool:
    content = render_firmware_identity(version, build_date, target_id)
    if os.path.isfile(out_path):
        with open(out_path, encoding="utf-8") as existing:
            if existing.read() == content:
                return False
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(content)
    return True


def _version_from_example_headers(project_dir: str) -> str:
    pat = re.compile(r'#\s*define\s+FIRMWARE_VERSION\s+"([^"]+)"')
    vals: set[str] = set()
    examples = os.path.join(project_dir, "examples")
    if not os.path.isdir(examples):
        return "dev"
    for name in os.listdir(examples):
        hdr = os.path.join(examples, name, "MyMesh.h")
        if not os.path.isfile(hdr):
            hdr = os.path.join(examples, name, "SensorMesh.h")
        if not os.path.isfile(hdr):
            continue
        with open(hdr, encoding="utf-8", errors="ignore") as f:
            for line in f:
                m = pat.search(line)
                if m:
                    vals.add(m.group(1))
    if len(vals) == 1:
        return next(iter(vals))
    return "dev"


def resolve_identity(
    project_dir: str,
    pio_env: str = "",
    *,
    version: str = "",
    build_date: str = "",
    target_id_raw: str = "",
) -> tuple[str, str, int]:
    version = version or os.environ.get("ENVYOS_FIRMWARE_VERSION", "")
    build_date = build_date or os.environ.get("ENVYOS_FIRMWARE_BUILD_DATE", "")
    target_raw = target_id_raw or os.environ.get("ENVYOS_MOTA_TARGET_ID", "")
    pio_env = pio_env or os.environ.get("PIOENV", "")

    if not version:
        version = _version_from_example_headers(project_dir)
    if not build_date:
        build_date = "dev build"
    if target_raw:
        target_id = int(target_raw, 0)
    elif pio_env:
        target_id = target_id_for_env(pio_env)
    else:
        target_id = 0
    return version, build_date, target_id


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="Output .cpp path")
    parser.add_argument("--version", default="", help="Firmware version label")
    parser.add_argument("--build-date", default="", help="UTC build stamp")
    parser.add_argument("--target-id", default="", help="MOTA target id (decimal or 0x hex)")
    parser.add_argument("--pio-env", default="", help="PlatformIO env (fallback target id hash)")
    parser.add_argument("--project-dir", default="", help="envycore root for header fallback")
    args = parser.parse_args()

    project_dir = args.project_dir or os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..")
    )
    version, build_date, target_id = resolve_identity(
        project_dir,
        args.pio_env,
        version=args.version,
        build_date=args.build_date,
        target_id_raw=args.target_id,
    )

    changed = write_firmware_identity(args.out, version, build_date, target_id)
    if changed:
        print(f"FirmwareIdentity: wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
